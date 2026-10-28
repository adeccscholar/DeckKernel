# SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#
# Resolve runtime DLLs from the versioned ThirdParty stack and the active
# C++Builder toolchain. Copy only DLLs imported by the actual executable.
#
# Windows/C++Builder: libc++-370.dll and libunwind-370.dll live beside
# bcc64x.exe, not in the ThirdParty packages.

function(adecc_runtime_search_directories output_dirs output_toolchain_dirs)
   if(NOT DEFINED ADECC_REPOSITORY_ROOT OR
      NOT DEFINED ADECC_THIRDPARTY_PACKAGE_ROOTS OR
      NOT CMAKE_BUILD_TYPE)
      message(FATAL_ERROR
         "Include Cache/cmake/ThirdParty.cmake before runtime deployment.")
   endif()

   set(_adecc_dirs)
   foreach(_root IN LISTS ADECC_THIRDPARTY_PACKAGE_ROOTS)
      # StackBuilder packages may contain variant-specific directories or
      # published flat Release runtimes (bin/). Both are project-pinned.
      # Search variant-specific paths first to avoid Debug/Release mixing.
      foreach(_suffix IN ITEMS
         "bin/win64/${CMAKE_BUILD_TYPE}"
         "bin/${CMAKE_BUILD_TYPE}"
         "bin")
         set(_dir "${_root}/${_suffix}")
         if(IS_DIRECTORY "${_dir}")
            list(APPEND _adecc_dirs "${_dir}")
         endif()
      endforeach()
   endforeach()
   if(NOT _adecc_dirs)
      message(FATAL_ERROR
         "No configuration-specific ThirdParty DLL directories found.")
   endif()

   # Resolve the actual compiler from this project's CMake configuration.
   # Do not hardcode a RAD Studio version or rely on global PATH.
   set(_adecc_toolchain_dirs)
   if(CMAKE_CXX_COMPILER)
      get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
      if(IS_DIRECTORY "${_compiler_dir}")
         list(APPEND _adecc_toolchain_dirs "${_compiler_dir}")
      endif()
   endif()
   if(DEFINED ENV{BDS} AND NOT "$ENV{BDS}" STREQUAL "")
      file(TO_CMAKE_PATH "$ENV{BDS}" _adecc_bds_root)
      foreach(_subdir IN ITEMS bin64 bin)
         if(IS_DIRECTORY "${_adecc_bds_root}/${_subdir}")
            list(APPEND _adecc_toolchain_dirs "${_adecc_bds_root}/${_subdir}")
         endif()
      endforeach()
   endif()
   if(NOT _adecc_toolchain_dirs)
      message(FATAL_ERROR
         "Cannot locate the C++Builder runtime directories; check CMAKE_CXX_COMPILER / BDS.")
   endif()

   list(REMOVE_DUPLICATES _adecc_toolchain_dirs)
   list(APPEND _adecc_dirs ${_adecc_toolchain_dirs})
   list(REMOVE_DUPLICATES _adecc_dirs)
   set(${output_dirs} "${_adecc_dirs}" PARENT_SCOPE)
   set(${output_toolchain_dirs} "${_adecc_toolchain_dirs}" PARENT_SCOPE)
endfunction()

function(adecc_install_pinned_runtime target component)
   if(NOT TARGET "${target}")
      message(FATAL_ERROR "Unknown runtime deployment target: ${target}")
   endif()

   adecc_runtime_search_directories(_adecc_dirs _adecc_toolchain_dirs)
   set(ADECC_RUNTIME_EXE "$<TARGET_FILE:${target}>")
   set(ADECC_RUNTIME_DIRS "${_adecc_dirs}")
   get_filename_component(ADECC_RUNTIME_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
   set(ADECC_RUNTIME_ZLIB_DIR "${ADECC_TP_ZLIB_ROOT}/bin/win64/${CMAKE_BUILD_TYPE}")
   set(ADECC_RUNTIME_RESOLVER "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ResolvePinnedRuntimeConflicts.cmake")
   # Only these source roots are eligible to supply copied runtime DLLs.
   # The operating system's own DLLs remain host-provided and are not installed.
   set(ADECC_RUNTIME_ALLOWED_ROOTS
      "${ADECC_REPOSITORY_ROOT}/ThirdParty;${_adecc_toolchain_dirs};$<TARGET_FILE_DIR:${target}>")

   set(_script_template [=[
# Generated for @ADECC_RUNTIME_EXE@ by adecc_install_pinned_runtime.
set(_adecc_executable [==[@ADECC_RUNTIME_EXE@]==])
set(_adecc_search_dirs [==[@ADECC_RUNTIME_DIRS@]==])
set(_adecc_allowed_roots [==[@ADECC_RUNTIME_ALLOWED_ROOTS@]==])
set(_adecc_compiler_dir [==[@ADECC_RUNTIME_COMPILER_DIR@]==])
set(_adecc_zlib_dir [==[@ADECC_RUNTIME_ZLIB_DIR@]==])
include([==[@ADECC_RUNTIME_RESOLVER@]==])
file(GET_RUNTIME_DEPENDENCIES
   EXECUTABLES "${_adecc_executable}"
   RESOLVED_DEPENDENCIES_VAR _adecc_resolved
   UNRESOLVED_DEPENDENCIES_VAR _adecc_unresolved
   CONFLICTING_DEPENDENCIES_PREFIX _adecc_conflict
   DIRECTORIES ${_adecc_search_dirs}
   PRE_EXCLUDE_REGEXES "^[Aa][Pp][Ii]-[Mm][Ss]-" "^[Ee][Xx][Tt]-[Mm][Ss]-"
   POST_EXCLUDE_REGEXES "[/\\\\][Ww]indows[/\\\\][Ss]ystem32[/\\\\]"
)
if(_adecc_unresolved)
   message(FATAL_ERROR
      "Unresolved runtime dependencies of ${_adecc_executable}: ${_adecc_unresolved}")
endif()
adecc_reconcile_runtime_dependencies(_adecc_resolved _adecc_conflict
   "${_adecc_compiler_dir}" "${_adecc_zlib_dir}")

# Preflight the entire shared runtime directory before touching any DLL.
# Long-running DeckKernel processes may have the same libraries loaded.
set(_adecc_install_sources)
set(_adecc_install_destinations)
set(_adecc_reused 0)
foreach(_dll IN LISTS _adecc_resolved)
   file(REAL_PATH "${_dll}" _dll_real)
   file(TO_CMAKE_PATH "${_dll_real}" _dll_normalized)
   string(TOLOWER "${_dll_normalized}" _dll_lower)
   set(_adecc_copy FALSE)
   foreach(_root IN LISTS _adecc_allowed_roots)
      file(REAL_PATH "${_root}" _root_real)
      file(TO_CMAKE_PATH "${_root_real}" _root_normalized)
      string(TOLOWER "${_root_normalized}" _root_lower)
      string(FIND "${_dll_lower}" "${_root_lower}/" _root_start)
      if(_root_start EQUAL 0)
         set(_adecc_copy TRUE)
         break()
      endif()
   endforeach()
   if(NOT _adecc_copy)
      message(STATUS "[RUNTIME] Host-provided DLL (not copied): ${_dll_real}")
      continue()
   endif()

   get_filename_component(_dll_name "${_dll_real}" NAME)
   set(_destination "${CMAKE_INSTALL_PREFIX}/${_dll_name}")
   if(EXISTS "${_destination}")
      file(SHA256 "${_dll_real}" _source_hash)
      file(SHA256 "${_destination}" _destination_hash)
      if(_source_hash STREQUAL _destination_hash)
         math(EXPR _adecc_reused "${_adecc_reused} + 1")
         message(STATUS "[RUNTIME] Reusing identical installed DLL: ${_dll_name}")
         continue()
      endif()
      # Never try replacing a DLL that another program may have loaded.
      message(FATAL_ERROR
         "[RUNTIME] Installed shared DLL differs from the pinned build: ${_destination}\n"
         "Source: ${_dll_real}\n"
         "Refusing to overwrite a potentially loaded DLL. Keep the running "
         "image downloader alive and deploy to a separate app directory, or "
         "stop all programs using the DLL before replacing it.")
   endif()
   list(APPEND _adecc_install_sources "${_dll_real}")
   list(APPEND _adecc_install_destinations "${_destination}")
endforeach()

set(_adecc_count 0)
list(LENGTH _adecc_install_sources _adecc_pending)
if(_adecc_pending GREATER 0)
   math(EXPR _adecc_last "${_adecc_pending} - 1")
   foreach(_index RANGE 0 ${_adecc_last})
      list(GET _adecc_install_sources ${_index} _source)
      list(GET _adecc_install_destinations ${_index} _destination)
      file(COPY_FILE "${_source}" "${_destination}"
         ONLY_IF_DIFFERENT RESULT _copy_result)
      if(NOT _copy_result STREQUAL "0")
         message(FATAL_ERROR
            "[RUNTIME] Failed to copy ${_source} to ${_destination}: "
            "${_copy_result}. Check folder permissions and running programs.")
      endif()
      math(EXPR _adecc_count "${_adecc_count} + 1")
      message(STATUS "[RUNTIME] Installed new DLL: ${_destination}")
   endforeach()
endif()
message(STATUS "[RUNTIME] New DLLs: ${_adecc_count}; reused identical DLLs: ${_adecc_reused}")
]=])
   string(CONFIGURE "${_script_template}" _script @ONLY)
   set(_script_file "${CMAKE_CURRENT_BINARY_DIR}/adecc_runtime_${target}_${component}.cmake")
   file(GENERATE OUTPUT "${_script_file}" CONTENT "${_script}")
   install(SCRIPT "${_script_file}" COMPONENT "${component}")
endfunction()


# Runs immediately after linking so CTest can start without an install.
# The only DLL sources are the pinned ThirdParty packages and BCC64X SDK.
function(adecc_stage_pinned_runtime target)
   if(NOT TARGET "${target}")
      message(FATAL_ERROR "Unknown runtime staging target: ${target}")
   endif()
   adecc_runtime_search_directories(_adecc_dirs _adecc_toolchain_dirs)
   set(ADECC_RUNTIME_EXE "$<TARGET_FILE:${target}>")
   set(ADECC_RUNTIME_OUTPUT "$<TARGET_FILE_DIR:${target}>")
   set(ADECC_RUNTIME_DIRS "${_adecc_dirs}")
   get_filename_component(ADECC_RUNTIME_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
   set(ADECC_RUNTIME_ZLIB_DIR "${ADECC_TP_ZLIB_ROOT}/bin/win64/${CMAKE_BUILD_TYPE}")
   set(ADECC_RUNTIME_RESOLVER "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ResolvePinnedRuntimeConflicts.cmake")
   set(ADECC_RUNTIME_ALLOWED_ROOTS
      "${ADECC_REPOSITORY_ROOT}/ThirdParty;${_adecc_toolchain_dirs}")

   set(_template [=[
set(_exe [==[@ADECC_RUNTIME_EXE@]==])
set(_output [==[@ADECC_RUNTIME_OUTPUT@]==])
set(_dirs [==[@ADECC_RUNTIME_DIRS@]==])
set(_roots [==[@ADECC_RUNTIME_ALLOWED_ROOTS@]==])
# Clear only files staged by this target in earlier builds, never manually
# installed files. Prevent loader precedence from selecting stale DLLs.
set(_manifest "${_output}/.deckkernel-runtime-closure.txt")
if(EXISTS "${_manifest}")
   file(STRINGS "${_manifest}" _previous ENCODING UTF-8)
   foreach(_name IN LISTS _previous)
      if(NOT _name MATCHES "^[A-Za-z0-9_+.-]+[.]dll$")
         message(FATAL_ERROR "Invalid previous runtime manifest entry: ${_name}")
      endif()
      file(REMOVE "${_output}/${_name}")
   endforeach()
endif()
set(_compiler_dir [==[@ADECC_RUNTIME_COMPILER_DIR@]==])
set(_zlib_dir [==[@ADECC_RUNTIME_ZLIB_DIR@]==])
include([==[@ADECC_RUNTIME_RESOLVER@]==])
file(GET_RUNTIME_DEPENDENCIES
   EXECUTABLES "${_exe}"
   RESOLVED_DEPENDENCIES_VAR _resolved
   UNRESOLVED_DEPENDENCIES_VAR _unresolved
   CONFLICTING_DEPENDENCIES_PREFIX _conflict
   DIRECTORIES ${_dirs}
   PRE_EXCLUDE_REGEXES "^[Aa][Pp][Ii]-[Mm][Ss]-" "^[Ee][Xx][Tt]-[Mm][Ss]-"
   POST_EXCLUDE_REGEXES "[/\\\\][Ww]indows[/\\\\][Ss]ystem32[/\\\\]")
if(_unresolved)
   message(FATAL_ERROR "Missing runtime imports for ${_exe}: ${_unresolved}")
endif()
adecc_reconcile_runtime_dependencies(_resolved _conflict
   "${_compiler_dir}" "${_zlib_dir}")
set(_count 0)
set(_copied)
foreach(_dll IN LISTS _resolved)
   file(REAL_PATH "${_dll}" _real)
   file(TO_CMAKE_PATH "${_real}" _normalized)
   string(TOLOWER "${_normalized}" _dll_lower)
   set(_allowed FALSE)
   foreach(_root IN LISTS _roots)
      file(REAL_PATH "${_root}" _root_real)
      file(TO_CMAKE_PATH "${_root_real}" _root_normalized)
      string(TOLOWER "${_root_normalized}" _root_lower)
      string(FIND "${_dll_lower}" "${_root_lower}/" _start)
      if(_start EQUAL 0)
         set(_allowed TRUE)
         break()
      endif()
   endforeach()
   if(_allowed)
      get_filename_component(_name "${_real}" NAME)
      file(COPY_FILE "${_real}" "${_output}/${_name}" ONLY_IF_DIFFERENT)
      message(STATUS "[RUNTIME] Staged ${_name}")
      list(APPEND _copied "${_name}")
      math(EXPR _count "${_count}+1")
   else()
      message(STATUS "[RUNTIME] Host provided: ${_real}")
   endif()
endforeach()
list(REMOVE_DUPLICATES _copied)
list(SORT _copied)
string(REPLACE ";" "\n" _manifest_text "${_copied}")
file(WRITE "${_manifest}" "${_manifest_text}\n")
message(STATUS "[RUNTIME] Staged ${_count} DLLs beside ${_exe}")
]=])
   string(CONFIGURE "${_template}" _script @ONLY)
   set(_script_file "${CMAKE_CURRENT_BINARY_DIR}/adecc_stage_${target}.cmake")
   file(GENERATE OUTPUT "${_script_file}" CONTENT "${_script}")
   add_custom_command(TARGET "${target}" POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -P "${_script_file}"
      COMMENT "Stage pinned runtime dependencies for ${target}"
      VERBATIM)
endfunction()
