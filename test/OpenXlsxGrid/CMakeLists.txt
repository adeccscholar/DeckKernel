cmake_minimum_required(VERSION 3.25)

# BCC64X and Ninja come from the pinned DeckKernel bootstrap.
get_filename_component(DECKKERNEL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(ADECC_BOOTSTRAP_TOOLS "${DECKKERNEL_ROOT}/Cache/BootstrapTools.cmake")
if(NOT EXISTS "${ADECC_BOOTSTRAP_TOOLS}")
   message(FATAL_ERROR "Run cmake -P bootstrap/Bootstrap.cmake from DeckKernel first.")
endif()
include("${ADECC_BOOTSTRAP_TOOLS}")
set(CMAKE_CXX_COMPILER "${ADECC_BCC64X_EXECUTABLE}" CACHE FILEPATH "DeckKernel C++ compiler")
set(CMAKE_MAKE_PROGRAM "${ADECC_NINJA_EXECUTABLE}" CACHE FILEPATH "DeckKernel Ninja")
project(DeckKernelOpenXlsxGridTest VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND
   NOT CMAKE_BUILD_TYPE STREQUAL "Release")
   message(FATAL_ERROR "Use -DCMAKE_BUILD_TYPE=Debug or Release.")
endif()
set(ADECC_THIRDPARTY_CONFIGURATION "${CMAKE_BUILD_TYPE}")
set(CMAKE_INSTALL_PREFIX "${DECKKERNEL_ROOT}/apps/${CMAKE_BUILD_TYPE}"
    CACHE PATH "DeckKernel deployment directory" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
include("${DECKKERNEL_ROOT}/Cache/cmake/ThirdParty.cmake")
include("${DECKKERNEL_ROOT}/Cache/cmake/InstallPinnedRuntime.cmake")

# Only the pinned DeckKernel bootstrap package is eligible for this build.
if(NOT DEFINED ADECC_TP_OPENXLSX_ROOT OR
   NOT DEFINED ADECC_TP_OPENXLSX_VERSION OR
   NOT EXISTS "${ADECC_TP_OPENXLSX_ROOT}")
   message(FATAL_ERROR
      "OpenXLSX 0.5.1 is missing from the bootstrapped DeckKernel ThirdParty stack. "
      "Run cmake -P bootstrap/Bootstrap.cmake, then reconfigure.")
endif()
if(NOT ADECC_TP_OPENXLSX_VERSION STREQUAL "0.5.1")
   message(FATAL_ERROR
      "Expected OpenXLSX 0.5.1, but the lock contains '${ADECC_TP_OPENXLSX_VERSION}'.")
endif()
if(DEFINED DECKKERNEL_OPENXLSX_ROOT AND
   NOT "${DECKKERNEL_OPENXLSX_ROOT}" STREQUAL "")
   message(STATUS "Ignoring the obsolete DECKKERNEL_OPENXLSX_ROOT override.")
endif()
set(DECKKERNEL_OPENXLSX_ROOT "${ADECC_TP_OPENXLSX_ROOT}")
set(_openxlsx_cmake
    "${DECKKERNEL_OPENXLSX_ROOT}/lib/win64/${CMAKE_BUILD_TYPE}/cmake/OpenXLSX")
if(NOT EXISTS "${_openxlsx_cmake}/OpenXLSXConfig.cmake")
   message(FATAL_ERROR "Missing OpenXLSX package config: ${_openxlsx_cmake}")
endif()
set(OpenXLSX_DIR "${_openxlsx_cmake}" CACHE PATH "Pinned OpenXLSX package" FORCE)
find_package(OpenXLSX 0.5.1 CONFIG REQUIRED PATHS "${_openxlsx_cmake}" NO_DEFAULT_PATH)
if(NOT TARGET OpenXLSX::OpenXLSX)
   message(FATAL_ERROR "Missing imported target OpenXLSX::OpenXLSX")
endif()

add_executable(adecc_openxlsx_grid_test main.cpp)
set_property(TARGET adecc_openxlsx_grid_test PROPERTY CXX_SCAN_FOR_MODULES OFF)
target_include_directories(adecc_openxlsx_grid_test PRIVATE "${DECKKERNEL_ROOT}")
target_compile_features(adecc_openxlsx_grid_test PRIVATE cxx_std_23)
target_compile_definitions(adecc_openxlsx_grid_test PRIVATE
   CHRONO_WORK_AROUND CHRONO_MISSED_ZONEDB)
target_compile_options(adecc_openxlsx_grid_test PRIVATE "-fborland-exec-code-page=65001")
target_link_libraries(adecc_openxlsx_grid_test PRIVATE OpenXLSX::OpenXLSX)

# Deploy only actual recursive DLL imports; no directory-wide Boost copy.
enable_testing()
list(JOIN _runtime_dirs ";" _test_path)
add_test(NAME OpenXlsxGridRoundtrip
   COMMAND "${CMAKE_COMMAND}" -E env
      "PATH=${_test_path};$ENV{PATH}"
      "$<TARGET_FILE:adecc_openxlsx_grid_test>")
set_tests_properties(OpenXlsxGridRoundtrip PROPERTIES
   WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
install(TARGETS adecc_openxlsx_grid_test RUNTIME DESTINATION .
   COMPONENT openxlsx_grid)
adecc_install_pinned_runtime(adecc_openxlsx_grid_test openxlsx_grid)
message(STATUS "Deployment directory: ${CMAKE_INSTALL_PREFIX}")
