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
      message(FATAL_ERROR "Unknown runtime installation target: ${target}")
   endif()
   install(DIRECTORY "$<TARGET_FILE_DIR:${target}>/"
      DESTINATION "."
      COMPONENT "${component}"
      FILES_MATCHING PATTERN "*.dll" PATTERN "*.DLL")
endfunction()


# Runs immediately after linking so CTest can start without an install.
# The only DLL sources are the pinned ThirdParty packages and BCC64X SDK.
function(adecc_stage_pinned_runtime target)
   if(NOT TARGET "${target}")
      message(FATAL_ERROR "Unknown runtime target: ${target}")
   endif()
   get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
   set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/runtime/${target}")
   set(_roots_file "${CMAKE_CURRENT_BINARY_DIR}/${target}.runtime-roots.txt")
   set(_toolchain_file "${CMAKE_CURRENT_BINARY_DIR}/${target}.runtime-toolchain.txt")
   string(REPLACE ";" "\n" _roots_text "${ADECC_THIRDPARTY_PACKAGE_ROOTS}")
   file(WRITE "${_roots_file}" "${_roots_text}\n")
   file(WRITE "${_toolchain_file}" "libc++-370.dll\nlibunwind-370.dll\n")
   add_custom_target("deckkernel-stage-${target}" ALL
      COMMAND "${CMAKE_COMMAND}"
         "-DADECC_RUNTIME_EXECUTABLE:FILEPATH=$<TARGET_FILE:${target}>"
         "-DADECC_RUNTIME_OUTPUT_DIR:PATH=$<TARGET_FILE_DIR:${target}>"
         "-DADECC_RUNTIME_ROOTS_FILE:FILEPATH=${_roots_file}"
         "-DADECC_RUNTIME_TOOLCHAIN_DLLS_FILE:FILEPATH=${_toolchain_file}"
         "-DADECC_RUNTIME_TOOLCHAIN_BIN:PATH=${_compiler_bin}"
         "-DADECC_RUNTIME_CONFIGURATION=${CMAKE_BUILD_TYPE}"
         -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageRuntimeClosure.cmake"
      VERBATIM)
   add_dependencies("deckkernel-stage-${target}" ${target})
endfunction()
