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
      set(_dir "${_root}/bin/win64/${CMAKE_BUILD_TYPE}")
      if(IS_DIRECTORY "${_dir}")
         list(APPEND _adecc_dirs "${_dir}")
      endif()
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
   # Only these source roots are eligible to supply copied runtime DLLs.
   # The operating system's own DLLs remain host-provided and are not installed.
   set(ADECC_RUNTIME_ALLOWED_ROOTS
      "${ADECC_REPOSITORY_ROOT}/ThirdParty;${_adecc_toolchain_dirs}")

   set(_script_template [=[
# Generated for @ADECC_RUNTIME_EXE@ by adecc_install_pinned_runtime.
set(_adecc_executable [==[@ADECC_RUNTIME_EXE@]==])
set(_adecc_search_dirs [==[@ADECC_RUNTIME_DIRS@]==])
set(_adecc_allowed_roots [==[@ADECC_RUNTIME_ALLOWED_ROOTS@]==])
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
if(_adecc_conflict_FILENAMES)
   message(FATAL_ERROR
      "Conflicting runtime dependencies: ${_adecc_conflict_FILENAMES}")
endif()

set(_adecc_count 0)
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
   if(_adecc_copy)
      file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}"
         TYPE SHARED_LIBRARY FILES "${_dll_real}")
      math(EXPR _adecc_count "${_adecc_count} + 1")
      message(STATUS "[RUNTIME] Required DLL: ${_dll_real}")
   else()
      message(STATUS "[RUNTIME] Host-provided DLL (not copied): ${_dll_real}")
   endif()
endforeach()
message(STATUS "[RUNTIME] Installed ${_adecc_count} required DLLs")
]=])
   string(CONFIGURE "${_script_template}" _script @ONLY)
   set(_script_file "${CMAKE_CURRENT_BINARY_DIR}/adecc_runtime_${target}_${component}.cmake")
   file(GENERATE OUTPUT "${_script_file}" CONTENT "${_script}")
   install(SCRIPT "${_script_file}" COMPONENT "${component}")
endfunction()
