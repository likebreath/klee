#===-- lib/Core/Makefile -----------------------------------*- Makefile -*--===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

LEVEL=../..

LIBRARYNAME=crete-replayer
DONT_BUILD_RELINKED=1
BUILD_ARCHIVE=1
NO_INSTALL=1

include $(LEVEL)/Makefile.common

CXX.Flags := $(filter-out -fno-rtti,$(CXX.Flags))
