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

project(
   DeckKernelCardScannerTest
   VERSION 0.1.0
   LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
   message(FATAL_ERROR
      "CMAKE_BUILD_TYPE must be Debug or Release for the firstconnect test."
   )
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND
   NOT CMAKE_BUILD_TYPE STREQUAL "Release")
   message(FATAL_ERROR
      "Unsupported CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'. Use Debug or Release."
   )
endif()

# Keep BuildEngine ThirdParty selection strictly coupled to this single-config
# Ninja build. This intentionally overrides a stale/cache-provided value.
set(ADECC_THIRDPARTY_CONFIGURATION "${CMAKE_BUILD_TYPE}")

set(
   CMAKE_INSTALL_PREFIX
   "${DECKKERNEL_ROOT}/apps/${CMAKE_BUILD_TYPE}"
   CACHE PATH
   "DeckKernel application deployment directory"
   FORCE
)

set(
   CMAKE_RUNTIME_OUTPUT_DIRECTORY
   "${CMAKE_BINARY_DIR}/bin"
)

include("${DECKKERNEL_ROOT}/Cache/cmake/ThirdParty.cmake")

message(STATUS "firstconnect build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "firstconnect runtime config   : ${ADECC_THIRDPARTY_CONFIGURATION}")

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(libpqxx CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

if(TARGET libpqxx::pqxx)
   set(DECKKERNEL_PQXX_TARGET libpqxx::pqxx)
elseif(TARGET pqxx)
   set(DECKKERNEL_PQXX_TARGET pqxx)
else()
   message(FATAL_ERROR "The libpqxx package did not expose a supported CMake target.")
endif()

find_package(OpenCV 5.0 CONFIG REQUIRED COMPONENTS core imgproc imgcodecs features)
add_executable(deckkernel_card_scantest main.cpp)
set_property(TARGET deckkernel_card_scantest PROPERTY CXX_SCAN_FOR_MODULES OFF)
target_compile_features(deckkernel_card_scantest PRIVATE cxx_std_23)
target_include_directories(deckkernel_card_scantest PRIVATE
   "${CMAKE_CURRENT_LIST_DIR}" ${OpenCV_INCLUDE_DIRS})
target_link_libraries(deckkernel_card_scantest PRIVATE
   PostgreSQL::PostgreSQL ${OpenCV_LIBS})

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

install(
   TARGETS deckkernel_card_scantest
   RUNTIME DESTINATION .
)

function(deckkernel_install_runtime_dlls thePackageRoot)
   install(
      DIRECTORY
         "${thePackageRoot}/bin/win64/${ADECC_THIRDPARTY_CONFIGURATION}/"
      DESTINATION .
      OPTIONAL
      FILES_MATCHING
         PATTERN "*.dll"
   )
endfunction()

deckkernel_install_runtime_dlls("${ADECC_TP_CURL_ROOT}")
deckkernel_install_runtime_dlls("${ADECC_TP_OPENSSL_ROOT}")
deckkernel_install_runtime_dlls("${ADECC_TP_LIBPQ_ROOT}")
deckkernel_install_runtime_dlls("${ADECC_TP_LIBPQXX_ROOT}")
deckkernel_install_runtime_dlls("${ADECC_TP_ZLIB_ROOT}")
deckkernel_install_runtime_dlls("${ADECC_TP_BROTLI_ROOT}")
deckkernel_install_runtime_dlls("${ADECC_TP_ZSTD_ROOT}")

# The packaged OpenCV DLL runtime closure is resolved through BuildEngine ThirdParty.
