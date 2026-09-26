cmake_minimum_required(VERSION 3.25)

get_filename_component(
   DECKKERNEL_ROOT
   "${CMAKE_CURRENT_LIST_DIR}/.."
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
