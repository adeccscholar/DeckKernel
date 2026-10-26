# SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#
# CMake GET_RUNTIME_DEPENDENCIES reports duplicate DLL names as conflicts,
# even when the files are byte-for-byte identical. Reconcile only identical
# copies, using the BCC64X SDK / pinned zlib as the authoritative providers.
#
# This script is included from generated build-tree and install scripts.
function(adecc_reconcile_runtime_dependencies resolved_variable conflict_prefix
         compiler_directory zlib_directory)
   set(_result "${${resolved_variable}}")
   set(_conflict_names "${${conflict_prefix}_FILENAMES}")

   # A copy already staged beside the EXE takes Windows loader precedence.
   # Verify it has the same content as the pinned source before trusting it.
   foreach(_path IN LISTS _result)
      get_filename_component(_name "${_path}" NAME)
      string(TOLOWER "${_name}" _lower)
      set(_authoritative "")
      if(_lower STREQUAL "libc++-370.dll" OR
         _lower STREQUAL "libunwind-370.dll")
         set(_authoritative "${compiler_directory}/${_name}")
      elseif(_lower STREQUAL "libz.dll")
         set(_authoritative "${zlib_directory}/${_name}")
      endif()
      if(NOT _authoritative)
         continue()
      endif()
      if(NOT EXISTS "${_authoritative}")
         message(FATAL_ERROR
            "[RUNTIME] Pinned runtime source missing: ${_authoritative}")
      endif()
      file(REAL_PATH "${_authoritative}" _canonical)
      file(REAL_PATH "${_path}" _actual)
      file(SHA256 "${_canonical}" _expected_hash)
      file(SHA256 "${_actual}" _actual_hash)
      if(NOT _expected_hash STREQUAL _actual_hash)
         message(FATAL_ERROR
            "[RUNTIME] A stale or incompatible DLL shadows the pinned runtime:\n"
            "  name: ${_name}\n"
            "  resolved: ${_actual} (SHA256=${_actual_hash})\n"
            "  pinned: ${_canonical} (SHA256=${_expected_hash})\n"
            "Do not overwrite a DLL used by the active image importer.")
      endif()
      list(REMOVE_ITEM _result "${_path}")
      list(APPEND _result "${_canonical}")
   endforeach()

   foreach(_name IN LISTS _conflict_names)
      set(_paths_var "${conflict_prefix}_${_name}")
      set(_paths "${${_paths_var}}")
      if(NOT _paths)
         message(FATAL_ERROR
            "[RUNTIME] Conflicting ${_name} had no recorded paths")
      endif()
      string(TOLOWER "${_name}" _lower)
      set(_authoritative "")
      if(_lower STREQUAL "libc++-370.dll" OR
         _lower STREQUAL "libunwind-370.dll")
         set(_authoritative "${compiler_directory}/${_name}")
      elseif(_lower STREQUAL "libz.dll")
         set(_authoritative "${zlib_directory}/${_name}")
      endif()

      if(_authoritative)
         if(NOT EXISTS "${_authoritative}")
            message(FATAL_ERROR
               "[RUNTIME] Pinned DLL for ${_name} is missing: ${_authoritative}")
         endif()
         file(REAL_PATH "${_authoritative}" _chosen)
      else()
         list(GET _paths 0 _first)
         file(REAL_PATH "${_first}" _chosen)
      endif()
      file(SHA256 "${_chosen}" _chosen_hash)
      set(_evidence "  chosen: ${_chosen} (SHA256=${_chosen_hash})\n")
      foreach(_candidate IN LISTS _paths)
         if(NOT EXISTS "${_candidate}")
            message(FATAL_ERROR
               "[RUNTIME] Conflicting DLL vanished: ${_candidate}")
         endif()
         file(REAL_PATH "${_candidate}" _real)
         file(SHA256 "${_real}" _hash)
         string(APPEND _evidence
            "  candidate: ${_real} (SHA256=${_hash})\n")
         if(NOT _hash STREQUAL _chosen_hash)
            message(FATAL_ERROR
               "[RUNTIME] Different binaries share the name ${_name}.\n"
               "${_evidence}"
               "The BuildEngine package/runtime versions must be reconciled. "
               "Refusing to hide a potential ABI conflict.")
         endif()
      endforeach()

      message(STATUS
         "[RUNTIME] Byte-identical ${_name} locations; canonical source: ${_chosen}")
      list(APPEND _result "${_chosen}")
   endforeach()

   list(REMOVE_DUPLICATES _result)
   set(${resolved_variable} "${_result}" PARENT_SCOPE)
endfunction()
