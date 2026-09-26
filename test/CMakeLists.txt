cmake_minimum_required(VERSION 3.25)

project(
   DeckKernelScryfallPostgresTest
   VERSION 0.1.0
   LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(DECKKERNEL_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(ADECC_THIRDPARTY_FILE
   "${DECKKERNEL_ROOT}/Cache/cmake/ThirdParty.cmake"
)

if(EXISTS "${ADECC_THIRDPARTY_FILE}")
   include("${ADECC_THIRDPARTY_FILE}")
else()
   message(
      STATUS
      "Cache/cmake/ThirdParty.cmake is not present; using the active CMake package search paths."
   )
endif()

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(libpqxx CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
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
