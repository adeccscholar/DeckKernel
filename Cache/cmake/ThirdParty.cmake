# Project-supplied integration for the bootstrapped BuildEngine ThirdParty stack.
# This file is committed. Package versions remain generated from Cache/thirdparty.lock.

get_filename_component(
   ADECC_REPOSITORY_ROOT
   "${CMAKE_CURRENT_LIST_DIR}/../.."
   ABSOLUTE
)

set(
   ADECC_PACKAGES_FILE
   "${ADECC_REPOSITORY_ROOT}/Cache/ThirdPartyPackages.cmake"
)

if(NOT EXISTS "${ADECC_PACKAGES_FILE}")
   message(FATAL_ERROR
      "Missing ${ADECC_PACKAGES_FILE}. Run: cmake -P bootstrap/Bootstrap.cmake"
   )
endif()

include("${ADECC_PACKAGES_FILE}")

if(NOT DEFINED ADECC_THIRDPARTY_CONFIGURATION)
   if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(ADECC_THIRDPARTY_CONFIGURATION "Debug")
   else()
      set(ADECC_THIRDPARTY_CONFIGURATION "Release")
   endif()
endif()

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")

foreach(_adecc_root IN LISTS ADECC_THIRDPARTY_PACKAGE_ROOTS)
   list(
      PREPEND CMAKE_PREFIX_PATH
      "${_adecc_root}"
      "${_adecc_root}/lib/cmake"
      "${_adecc_root}/lib/win64/${ADECC_THIRDPARTY_CONFIGURATION}"
      "${_adecc_root}/lib/win64/${ADECC_THIRDPARTY_CONFIGURATION}/cmake"
   )

   list(PREPEND CMAKE_INCLUDE_PATH "${_adecc_root}/include")
   list(
      PREPEND CMAKE_LIBRARY_PATH
      "${_adecc_root}/lib/win64/${ADECC_THIRDPARTY_CONFIGURATION}"
   )
endforeach()

set(CURL_ROOT          "${ADECC_TP_CURL_ROOT}")
set(PostgreSQL_ROOT    "${ADECC_TP_LIBPQ_ROOT}")
set(libpqxx_ROOT       "${ADECC_TP_LIBPQXX_ROOT}")
set(nlohmann_json_ROOT "${ADECC_TP_NLOHMANN_JSON_ROOT}")
set(OPENSSL_ROOT_DIR   "${ADECC_TP_OPENSSL_ROOT}")
set(OpenSSL_ROOT       "${ADECC_TP_OPENSSL_ROOT}")
set(ZLIB_ROOT          "${ADECC_TP_ZLIB_ROOT}")

if(NOT TARGET nlohmann_json::nlohmann_json)
   add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
   set_target_properties(
      nlohmann_json::nlohmann_json
      PROPERTIES
         INTERFACE_INCLUDE_DIRECTORIES "${ADECC_TP_NLOHMANN_JSON_ROOT}/include"
   )
endif()

message(STATUS "adecc repository root       : ${ADECC_REPOSITORY_ROOT}")
message(STATUS "adecc ThirdParty config     : ${ADECC_THIRDPARTY_CONFIGURATION}")
message(STATUS "adecc curl root             : ${ADECC_TP_CURL_ROOT}")
message(STATUS "adecc OpenSSL root          : ${ADECC_TP_OPENSSL_ROOT}")
message(STATUS "adecc PostgreSQL/libpq root : ${ADECC_TP_LIBPQ_ROOT}")
message(STATUS "adecc libpqxx root          : ${ADECC_TP_LIBPQXX_ROOT}")
