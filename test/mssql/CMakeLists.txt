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

if(NOT CMAKE_BUILD_TYPE)
   message(FATAL_ERROR "CMAKE_BUILD_TYPE must be Debug or Release.")
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND
   NOT CMAKE_BUILD_TYPE STREQUAL "Release")
   message(FATAL_ERROR
      "Unsupported CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'. Use Debug or Release."
   )
endif()

set(
   CMAKE_INSTALL_PREFIX
   "${DECKKERNEL_ROOT}/apps/${CMAKE_BUILD_TYPE}"
   CACHE PATH
   "DeckKernel test deployment directory"
   FORCE
)

# OpenXLSX 0.5.1 is supplied exclusively by the pinned DeckKernel bootstrap.
# The MSSQL teaching executable uses the same package as test/OpenXlsxGrid.
set(ADECC_THIRDPARTY_CONFIGURATION "${CMAKE_BUILD_TYPE}")
include("${DECKKERNEL_ROOT}/Cache/cmake/ThirdParty.cmake")
include("${DECKKERNEL_ROOT}/Cache/cmake/InstallPinnedRuntime.cmake")
if(NOT DEFINED ADECC_TP_OPENXLSX_ROOT OR
   NOT ADECC_TP_OPENXLSX_VERSION STREQUAL "0.5.1")
   message(FATAL_ERROR
      "OpenXLSX 0.5.1 is missing from the pinned DeckKernel stack. "
      "Run cmake -P bootstrap/Bootstrap.cmake.")
endif()
set(_openxlsx_cmake
   "${ADECC_TP_OPENXLSX_ROOT}/lib/win64/${CMAKE_BUILD_TYPE}/cmake/OpenXLSX")
if(NOT EXISTS "${_openxlsx_cmake}/OpenXLSXConfig.cmake")
   message(FATAL_ERROR "Missing OpenXLSX package: ${_openxlsx_cmake}")
endif()
set(OpenXLSX_DIR "${_openxlsx_cmake}" CACHE PATH "Pinned OpenXLSX 0.5.1" FORCE)
find_package(OpenXLSX 0.5.1 CONFIG REQUIRED
   PATHS "${_openxlsx_cmake}" NO_DEFAULT_PATH)
if(NOT TARGET OpenXLSX::OpenXLSX)
   message(FATAL_ERROR "OpenXLSX::OpenXLSX imported target is missing")
endif()

add_executable(
   adecc_mssql_odbc_test
   test_mssql_odbc_database.cpp
   "${DECKKERNEL_ROOT}/adecc/mssql/mssql_odbc_database.h"
)

# Independent teaching example: CSV -> typed policies -> SQL Server.
add_executable(
   adecc_mssql_csv_pipeline
   csv_booking_pipeline.cpp
)
target_compile_definitions(adecc_mssql_csv_pipeline PRIVATE
   CHRONO_WORK_AROUND CHRONO_MISSED_ZONEDB)
target_include_directories(adecc_mssql_csv_pipeline PRIVATE
   "${DECKKERNEL_ROOT}/adecc/cpp_core"
   "${DECKKERNEL_ROOT}/adecc/mssql"
   "${DECKKERNEL_ROOT}/adecc/cpp_domain"
   "${DECKKERNEL_ROOT}/adecc/openXlsxTools")
target_compile_options(adecc_mssql_csv_pipeline PRIVATE
   "-fborland-exec-code-page=65001")
target_link_libraries(adecc_mssql_csv_pipeline PRIVATE OpenXLSX::OpenXLSX)
if(WIN32)
   target_link_libraries(adecc_mssql_csv_pipeline PRIVATE odbc32)
else()
   target_link_libraries(adecc_mssql_csv_pipeline PRIVATE ODBC::ODBC)
endif()
install(TARGETS adecc_mssql_csv_pipeline RUNTIME DESTINATION . COMPONENT csv_pipeline)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/bookings.csv" DESTINATION . COMPONENT csv_pipeline)

# Deploy only actual recursive runtime dependencies from the pinned stack.
adecc_install_pinned_runtime(adecc_mssql_csv_pipeline csv_pipeline)

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

install(
   TARGETS adecc_mssql_odbc_test
   RUNTIME DESTINATION .
   COMPONENT odbc_test
)
