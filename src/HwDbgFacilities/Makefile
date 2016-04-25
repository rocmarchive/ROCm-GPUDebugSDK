# Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.

# Note: This makefile is hardwired to build 64bit only.
#
# To build this file for debug locally, you can do make -e HSAIL_build=debug
override ARCH_SUFFIX=-x64
ifeq (${HSAIL_build}, debug)
    override ARCH_SUFFIX=-x64-d
endif

# To force libelf.a to be included into the HwDbgFacilities and to be used
# inspite of the runtime's libelf.so, we need to do multple steps
# 1) We restrict how we search for symbols (-B-symbolic)
# 2) We include the whole archive (--whole-archive) into the dynamic library
#
# Unsure if -B-symbolic has any other side effects and it behaves similarly everywhere
#
AMDTLIBDWARFDIR=./AMDTLibDWARF
AMDTLIBDWARFINC=$(AMDTLIBDWARFDIR)/include
AMDTLIBDWARFCOMMONINC=$(AMDTLIBDWARFINC)/common
AMDTLIBDWARFLIBDIR=$(AMDTLIBDWARFDIR)/lib/x86_64

AMDTLIBDWARFLINKCMD= -Wl,--whole-archive $(AMDTLIBDWARFLIBDIR)/libelf.a $(AMDTLIBDWARFLIBDIR)/libdwarf.a -Wl,--no-whole-archive

DEBUGFACINC=./include/

INCLUDEDIRS= \
	-I$(DEBUGFACINC) -I./\
	-I$(AMDTLIBDWARFINC) -I$(AMDTLIBDWARFCOMMONINC) \
	-I/usr/include/

# Compiler Info
CC=g++

CFLAGS= $(INCLUDEDIRS) -g -fPIC -m64 -Wall -std=c++11

# -B-symbolic added for libelf
LDFLAGS= -g -shared -Wl,-Bsymbolic -Wl,-Bsymbolic-functions


SOURCES=\
    DbgInfoDwarfParser.cpp\
    DbgInfoUtils.cpp\
    DbgInfoLines.cpp\
    FacilitiesInterface.cpp

OBJECTS=$(SOURCES:.cpp=.o)

OUTPUTLIB=../../lib/x86_64

hsa: $(OBJECTS)
	mkdir -p $(OUTPUTLIB)
	$(CC) $(LDFLAGS) $(OBJECTS) $(AMDTLIBDWARFLINKCMD) -o $(OUTPUTLIB)/libAMDHwDbgFacilities$(ARCH_SUFFIX).so

.cpp.o:
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(OUTPUTLIB)/libAMDHwDbgFacilities$(ARCH_SUFFIX).so
	rm -f *.o
	rm -f *.os
	rm -f *.d
