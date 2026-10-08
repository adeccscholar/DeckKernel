cmake_minimum_required(VERSION 3.25)

get_filename_component(
   DECKKERNEL_ROOT
   "${CMAKE_CURRENT_LIST_DIR}/../.."
   ABSOLUTE
)

set(
   ADECC_BOOTSTRAP_TOOLS
   "${DECKKERNEL_ROOT}/Cache/BootstrapTools.cmake"
)

if(NOT EXISTS "${ADECC_BOOTSTRAP_TOOLS}")
   message(FATAL_ERROR
      "Missing ${ADECC_BOOTSTRAP_TOOLS}. Run: cmake -P bootstrap/Bootstrap.cmake"
   )
endif()

include("${ADECC_BOOTSTRAP_TOOLS}")

set(
   CMAKE_CXX_COMPILER
   "${ADECC_BCC64X_EXECUTABLE}"
   CACHE FILEPATH
   "DeckKernel C++ compiler"
)

set(
   CMAKE_MAKE_PROGRAM
   "${ADECC_NINJA_EXECUTABLE}"
   CACHE FILEPATH
   "DeckKernel Ninja executable"
)

project(adecc_postgresql_pqxx_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
   message(FATAL_ERROR "CMAKE_BUILD_TYPE must be Debug or Release.")
endif()

set(ADECC_THIRDPARTY_CONFIGURATION "${CMAKE_BUILD_TYPE}")
include("${DECKKERNEL_ROOT}/Cache/cmake/ThirdParty.cmake")

find_package(PostgreSQL REQUIRED)
find_package(libpqxx CONFIG REQUIRED)

if(TARGET libpqxx::pqxx)
   set(ADECC_PQXX_TARGET libpqxx::pqxx)
elseif(TARGET pqxx)
   set(ADECC_PQXX_TARGET pqxx)
else()
   message(FATAL_ERROR "The libpqxx package did not expose a supported CMake target.")
endif()

add_executable(
   adecc_postgresql_pqxx_test
   test_postgresql_pqxx_database.cpp
   "${DECKKERNEL_ROOT}/adecc/postgre/pqxx_database.h"
)

target_compile_definitions(
   adecc_postgresql_pqxx_test
   PRIVATE
      CHRONO_WORK_AROUND
      CHRONO_MISSED_ZONEDB
)

target_include_directories(
   adecc_postgresql_pqxx_test
   PRIVATE
      "${DECKKERNEL_ROOT}/adecc/cpp_core"
      "${DECKKERNEL_ROOT}/adecc/postgre"
)

target_link_libraries(
   adecc_postgresql_pqxx_test
   PRIVATE
      PostgreSQL::PostgreSQL
      ${ADECC_PQXX_TARGET}
)

function(adecc_copy_runtime_dlls thePackageRoot)
   file(
      GLOB ADECC_RUNTIME_DLLS
      "${thePackageRoot}/bin/win64/${ADECC_THIRDPARTY_CONFIGURATION}/*.dll"
   )

   if(ADECC_RUNTIME_DLLS)
      add_custom_command(
         TARGET adecc_postgresql_pqxx_test
         POST_BUILD
         COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${ADECC_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:adecc_postgresql_pqxx_test>"
         COMMAND_EXPAND_LISTS
      )
   endif()
endfunction()

adecc_copy_runtime_dlls("${ADECC_TP_LIBPQ_ROOT}")
adecc_copy_runtime_dlls("${ADECC_TP_LIBPQXX_ROOT}")
adecc_copy_runtime_dlls("${ADECC_TP_OPENSSL_ROOT}")
adecc_copy_runtime_dlls("${ADECC_TP_ZLIB_ROOT}")
