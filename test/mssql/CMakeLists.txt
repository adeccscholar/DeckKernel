cmake_minimum_required(VERSION 3.25)

project(adecc_mssql_odbc_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ADECC_CPP_CORE_DIR "../../adecc/cpp_core")
set(ADECC_MSSQL_ODBC_DIR "../../adecc/mssql")

if(NOT ADECC_CPP_CORE_DIR)
   message(FATAL_ERROR "ADECC_CPP_CORE_DIR must point to the cpp_core include directory")
endif()

if(NOT ADECC_MSSQL_ODBC_DIR)
   message(FATAL_ERROR "ADECC_MSSQL_ODBC_DIR must point to the SQL Server ODBC adapter directory")
endif()


add_executable(adecc_mssql_odbc_test
   test_mssql_odbc_database.cpp
)

target_compile_definitions(adecc_mssql_odbc_test PRIVATE CHRONO_WORK_AROUND CHRONO_MISSED_ZONEDB)
target_compile_options(adecc_mssql_odbc_test PRIVATE -fborland-exec-code-page=65001)

target_include_directories(adecc_mssql_odbc_test PRIVATE
   "${ADECC_CPP_CORE_DIR}"
   "${ADECC_MSSQL_ODBC_DIR}"
)

if(WIN32)
   target_link_libraries(adecc_mssql_odbc_test PRIVATE odbc32)
else()
   find_package(ODBC REQUIRED)
   target_link_libraries(adecc_mssql_odbc_test PRIVATE ODBC::ODBC)
endif()
