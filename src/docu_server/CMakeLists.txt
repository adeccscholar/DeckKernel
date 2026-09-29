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
   DeckKernelDocuServer
   VERSION 0.1.0
   LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
   message(FATAL_ERROR
      "CMAKE_BUILD_TYPE must be Debug or Release for the documentation server."
   )
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND
   NOT CMAKE_BUILD_TYPE STREQUAL "Release")
   message(FATAL_ERROR
      "Unsupported CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}'. Use Debug or Release."
   )
endif()

set(ADECC_THIRDPARTY_CONFIGURATION "${CMAKE_BUILD_TYPE}")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
   set(
      CMAKE_INSTALL_PREFIX
      "${DECKKERNEL_ROOT}/apps/${CMAKE_BUILD_TYPE}"
      CACHE PATH
      "DeckKernel application deployment directory"
      FORCE
   )
endif()

include("${DECKKERNEL_ROOT}/Cache/cmake/ThirdParty.cmake")

if(NOT DEFINED ADECC_TP_BOOST_ROOT)
   message(FATAL_ERROR "Boost is missing from the bootstrapped ThirdParty stack.")
endif()

if(NOT DEFINED ADECC_TP_CMARK_GFM_ROOT)
   message(FATAL_ERROR "cmark-gfm is missing from the bootstrapped ThirdParty stack.")
endif()

add_executable(
   deckkernel_docu_server
   main.cpp
   docu_server.cpp
   docu_server.h
   markdown_renderer.cpp
   markdown_renderer.h
)

target_include_directories(
   deckkernel_docu_server
   PRIVATE
      ${ADECC_TP_BOOST_INCLUDE_DIRS}
)

target_compile_definitions(
   deckkernel_docu_server
   PRIVATE
      BOOST_ALL_NO_LIB
      BOOST_ERROR_CODE_HEADER_ONLY
      _WIN32_WINNT=0x0A00
)

target_link_libraries(
   deckkernel_docu_server
   PRIVATE
      ws2_32
      mswsock
)

install(
   TARGETS deckkernel_docu_server
   RUNTIME DESTINATION .
)

install(
   DIRECTORY
      "${ADECC_TP_CMARK_GFM_ROOT}/bin/win64/${ADECC_THIRDPARTY_CONFIGURATION}/"
   DESTINATION .
   OPTIONAL
   FILES_MATCHING
      PATTERN "*.dll"
)

message(STATUS "docu-server build type       : ${CMAKE_BUILD_TYPE}")
message(STATUS "docu-server install prefix   : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "docu-server cmark-gfm root   : ${ADECC_TP_CMARK_GFM_ROOT}")
