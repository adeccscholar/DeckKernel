# Project-owned bootstrap configuration.
#
# This file is committed with the project. The project maintainer sets the
# package repository once; end users do not have to provide paths or URLs.

if(NOT DEFINED ADECC_PACKAGE_REPOSITORY)
   set(ADECC_PACKAGE_REPOSITORY "https://github.com/adeccscholar/DeckKernel")
endif()

if(NOT DEFINED ADECC_REQUIRED_TOOLCHAIN)
   set(ADECC_REQUIRED_TOOLCHAIN "bcc64x")
endif()

# Pinned Ninja used by the BCC64X CMake workflow. If the same version is not
# already available in the active environment, Bootstrap.cmake downloads the
# official Windows x64 binary and verifies both size and SHA-256.
if(NOT DEFINED ADECC_NINJA_VERSION)
   set(ADECC_NINJA_VERSION "1.13.2")
endif()

if(NOT DEFINED ADECC_NINJA_URL)
   set(ADECC_NINJA_URL "https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip")
endif()

if(NOT DEFINED ADECC_NINJA_ARCHIVE_SIZE)
   set(ADECC_NINJA_ARCHIVE_SIZE "291570")
endif()

if(NOT DEFINED ADECC_NINJA_SHA256)
   set(ADECC_NINJA_SHA256 "07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65")
endif()
