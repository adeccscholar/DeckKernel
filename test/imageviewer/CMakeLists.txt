cmake_minimum_required(VERSION 3.25)
get_filename_component(DECKKERNEL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(ADECC_BOOTSTRAP_TOOLS "${DECKKERNEL_ROOT}/Cache/BootstrapTools.cmake")
if(NOT EXISTS "${ADECC_BOOTSTRAP_TOOLS}")
   message(FATAL_ERROR "Run: cmake -P bootstrap/Bootstrap.cmake")
endif()
include("${ADECC_BOOTSTRAP_TOOLS}")
set(CMAKE_CXX_COMPILER "${ADECC_BCC64X_EXECUTABLE}" CACHE FILEPATH "BCC64X")
set(CMAKE_MAKE_PROGRAM "${ADECC_NINJA_EXECUTABLE}" CACHE FILEPATH "Ninja")
project(DeckKernelImageViewer VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
   message(FATAL_ERROR "CMAKE_BUILD_TYPE must be Debug or Release")
endif()
set(ADECC_THIRDPARTY_CONFIGURATION "${CMAKE_BUILD_TYPE}")
set(CMAKE_INSTALL_PREFIX "${DECKKERNEL_ROOT}/apps/${CMAKE_BUILD_TYPE}"
   CACHE PATH "Viewer installation directory" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
include("${DECKKERNEL_ROOT}/Cache/cmake/ThirdParty.cmake")
find_package(PostgreSQL REQUIRED)
find_package(OpenSSL REQUIRED)
add_executable(deckkernel_image_viewer WIN32 image_viewer.cpp)
target_compile_features(deckkernel_image_viewer PRIVATE cxx_std_23)
target_compile_definitions(deckkernel_image_viewer PRIVATE
   UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
   CHRONO_WORK_AROUND CHRONO_MISSED_ZONEDB)
target_link_libraries(deckkernel_image_viewer PRIVATE
   PostgreSQL::PostgreSQL OpenSSL::Crypto
   ole32 uuid gdi32 user32 shell32)
install(TARGETS deckkernel_image_viewer RUNTIME DESTINATION .)
foreach(_package IN ITEMS
   ADECC_TP_LIBPQ_ROOT ADECC_TP_OPENSSL_ROOT ADECC_TP_ZLIB_ROOT
   ADECC_TP_BROTLI_ROOT ADECC_TP_ZSTD_ROOT)
   if(DEFINED ${_package})
      install(DIRECTORY "${${_package}}/bin/win64/${ADECC_THIRDPARTY_CONFIGURATION}/"
         DESTINATION . OPTIONAL FILES_MATCHING PATTERN "*.dll")
   endif()
endforeach()
