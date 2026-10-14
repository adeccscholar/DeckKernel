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

# The current DeckKernel thirdparty.lock does not contain OpenXLSX 0.5.1.
# Accept a genuine versioned package root; never select an arbitrary system DLL.
if(NOT DECKKERNEL_OPENXLSX_ROOT)
   if(ADECC_TP_OPENXLSX_ROOT)
      set(DECKKERNEL_OPENXLSX_ROOT "${ADECC_TP_OPENXLSX_ROOT}")
   elseif(DEFINED ENV{DECKKERNEL_OPENXLSX_ROOT} AND
          NOT "$ENV{DECKKERNEL_OPENXLSX_ROOT}" STREQUAL "")
      set(DECKKERNEL_OPENXLSX_ROOT "$ENV{DECKKERNEL_OPENXLSX_ROOT}")
   elseif(EXISTS "${DECKKERNEL_ROOT}/ThirdParty/openxlsx/0.5.1")
      set(DECKKERNEL_OPENXLSX_ROOT "${DECKKERNEL_ROOT}/ThirdParty/openxlsx/0.5.1")
   else()
      message(FATAL_ERROR
         "OpenXLSX 0.5.1 is not in the DeckKernel lock. "
         "Pass -DDECKKERNEL_OPENXLSX_ROOT=<BuildEngine install/packages/openxlsx/0.5.1>.")
   endif()
endif()
get_filename_component(DECKKERNEL_OPENXLSX_ROOT "${DECKKERNEL_OPENXLSX_ROOT}" ABSOLUTE)
set(_openxlsx_cmake
    "${DECKKERNEL_OPENXLSX_ROOT}/lib/win64/${CMAKE_BUILD_TYPE}/cmake/OpenXLSX")
if(NOT EXISTS "${_openxlsx_cmake}/OpenXLSXConfig.cmake")
   message(FATAL_ERROR "Missing OpenXLSX package config: ${_openxlsx_cmake}")
endif()
set(OpenXLSX_DIR "${_openxlsx_cmake}" CACHE PATH "OpenXLSX versioned package" FORCE)
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

# Install the matching DLL and the complete known library runtime closure.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
   set(_openxlsx_dll "libOpenXLSXd.dll")
else()
   set(_openxlsx_dll "libOpenXLSX.dll")
endif()
set(_openxlsx_dll_file
   "${DECKKERNEL_OPENXLSX_ROOT}/bin/win64/${CMAKE_BUILD_TYPE}/${_openxlsx_dll}")
if(NOT EXISTS "${_openxlsx_dll_file}")
   message(FATAL_ERROR "Required OpenXLSX runtime DLL missing: ${_openxlsx_dll_file}")
endif()
set(_runtime_dirs "${DECKKERNEL_OPENXLSX_ROOT}/bin/win64/${CMAKE_BUILD_TYPE}")
set(_package_roots
   "${ADECC_TP_PUGIXML_ROOT}"
   "${ADECC_TP_LIBZIP_ROOT}"
   "${ADECC_TP_BOOST_ROOT}"
   "${ADECC_TP_ZLIB_ROOT}"
   "${ADECC_TP_ZSTD_ROOT}"
   "${ADECC_TP_BZIP2_ROOT}"
   "${ADECC_TP_XZ_ROOT}"
   "${ADECC_TP_OPENSSL_ROOT}"
   "${ADECC_TP_BROTLI_ROOT}"
)
foreach(_root IN LISTS _package_roots)
   if(_root AND EXISTS "${_root}/bin/win64/${CMAKE_BUILD_TYPE}")
      list(APPEND _runtime_dirs "${_root}/bin/win64/${CMAKE_BUILD_TYPE}")
      install(DIRECTORY "${_root}/bin/win64/${CMAKE_BUILD_TYPE}/"
              DESTINATION . FILES_MATCHING PATTERN "*.dll")
   endif()
endforeach()
enable_testing()
list(JOIN _runtime_dirs ";" _test_path)
add_test(NAME OpenXlsxGridRoundtrip
   COMMAND "${CMAKE_COMMAND}" -E env
      "PATH=${_test_path};$ENV{PATH}"
      "$<TARGET_FILE:adecc_openxlsx_grid_test>")
set_tests_properties(OpenXlsxGridRoundtrip PROPERTIES
   WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
install(TARGETS adecc_openxlsx_grid_test RUNTIME DESTINATION .)
install(FILES "${_openxlsx_dll_file}" DESTINATION .)
message(STATUS "OpenXLSX selected DLL: ${_openxlsx_dll_file}")
message(STATUS "Deployment directory: ${CMAKE_INSTALL_PREFIX}")
