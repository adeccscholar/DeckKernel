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
