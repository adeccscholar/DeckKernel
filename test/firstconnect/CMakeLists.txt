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
   DeckKernelScryfallPostgresTest
   VERSION 0.1.0
   LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(
   CMAKE_RUNTIME_OUTPUT_DIRECTORY
   "${CMAKE_BINARY_DIR}/bin"
)

include("${DECKKERNEL_ROOT}/Cache/cmake/ThirdParty.cmake")

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

add_executable(
   deckkernel_scryfall_postgres_test
   scryfall_postgres_test.cpp
)

target_include_directories(
   deckkernel_scryfall_postgres_test
   PRIVATE
      "${CMAKE_CURRENT_LIST_DIR}"
      "${DECKKERNEL_ROOT}/adecc/cpp_core"
)

target_compile_definitions(
   deckkernel_scryfall_postgres_test
   PRIVATE
      CHRONO_WORK_AROUND
      CHRONO_MISSED_ZONEDB
)

target_link_libraries(
   deckkernel_scryfall_postgres_test
   PRIVATE
      CURL::libcurl
      OpenSSL::SSL
      OpenSSL::Crypto
      PostgreSQL::PostgreSQL
      ${DECKKERNEL_PQXX_TARGET}
      nlohmann_json::nlohmann_json
      ZLIB::ZLIB
)


# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

install(
   TARGETS deckkernel_scryfall_postgres_test
   RUNTIME DESTINATION bin
)

function(deckkernel_install_runtime_dlls thePackageRoot)
   install(
      DIRECTORY
         "${thePackageRoot}/bin/win64/${ADECC_THIRDPARTY_CONFIGURATION}/"
      DESTINATION bin
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
