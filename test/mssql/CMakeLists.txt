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

project(adecc_mssql_odbc_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(
   adecc_mssql_odbc_test
   test_mssql_odbc_database.cpp
   "${DECKKERNEL_ROOT}/adecc/mssql/mssql_odbc_database.h"
)

target_compile_definitions(
   adecc_mssql_odbc_test
   PRIVATE
      CHRONO_WORK_AROUND
      CHRONO_MISSED_ZONEDB
)

target_include_directories(
   adecc_mssql_odbc_test
   PRIVATE
      "${DECKKERNEL_ROOT}/adecc/cpp_core"
      "${DECKKERNEL_ROOT}/adecc/mssql"
)

if(WIN32)
   target_link_libraries(adecc_mssql_odbc_test PRIVATE odbc32)
else()
   find_package(ODBC REQUIRED)
   target_link_libraries(adecc_mssql_odbc_test PRIVATE ODBC::ODBC)
endif()
