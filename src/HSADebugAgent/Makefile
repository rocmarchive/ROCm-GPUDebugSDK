# Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.

# Note: This makefile is hardwired to build 64bit only.
#
# To build this file for debug locally, call "make -e HSAIL_build=debug"

ARCH_SUFFIX=x64
ifeq (${HSAIL_build}, debug)
    ARCH_SUFFIX=x64-d
endif

HSADIR=/opt/rocm/hsa
HSAINC=$(HSADIR)/include/hsa/
HSALIB=$(HSADIR)/lib/

# To force libelf.a to be included into the Agent and be used inspite
# of the runtime or any other libelf.so, we need to do multple steps
# 1) We restrict how we search for symbols (-B-symbolic)
# 2) We include the whole archive (--whole-archive) into the dynamic library
#
# Unsure if -B-symbolic has any other side effects and it behaves similarly everywhere
LIBELFINC=../../include
LIBELFCOMMONINC=../../include/common
LIBELFLIBDIR=../../lib/x86_64
LIBELFSTATIC= -Wl,--whole-archive $(LIBELFLIBDIR)/libelf.a -Wl,--no-whole-archive

HWDBGLIB=../../lib/x86_64
HWDBGINC=../../include

# The header files for the agent are within an Include/ directory in the Agent/
HSAAGENTINC=Include/

DYNAMICLIBMODULEDIR=../DynamicLibraryModule

INCLUDEDIRS= -I$(HSAINC) \
	-I$(LIBELFINC) -I$(LIBELFCOMMONINC) \
	-I$(HWDBGINC) -I$(HSAAGENTINC) -I$(DYNAMICLIBMODULEDIR)

# Compiler Info
CC=g++
# Remove / add -DENABLE_LOGGING to disable tracing
CFLAGS= -g -fPIC -m64 -Wall -std=c++11 -DAMD_INTERNAL_BUILD -DFUTURE_ROCR_VERSION $(INCLUDEDIRS)
# -B-symbolic added for libelf
LDFLAGS= -g -shared -pthread -Wl,-Bsymbolic -Wl,-Bsymbolic-functions

# Local location of the DBE library within the SDK
DBEHSAPATH=-L$(HWDBGLIB)

## Local library name
DBEHSALIBNAME=-lAMDGPUDebugHSA-x64

SOURCES=\
	$(DYNAMICLIBMODULEDIR)/HSADebuggerRTModule.cpp\
	$(DYNAMICLIBMODULEDIR)/DynamicLibraryModule.cpp\
	PrePostDispatchCallback.cpp\
	AgentBreakpoint.cpp\
	AgentBreakpointManager.cpp\
	AgentBinary.cpp\
	AgentFocusWaveControl.cpp\
	AgentContext.cpp\
	AgentConfiguration.cpp\
	AgentISABuffer.cpp\
	AgentProcessPacket.cpp\
	AgentLogging.cpp\
	AgentNotifyGdb.cpp\
	AgentSegmentLoader.cpp\
	AgentUtils.cpp\
	AgentWavePrinter.cpp\
	CommunicationControl.cpp\
	CommandLoop.cpp\
	HSADebugAgent.cpp\
	HSAIntercept.cpp

OBJECTS=$(SOURCES:.cpp=.o)

OUTPUTAGENTDIR=../../lib/x86_64

hsa: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBELFSTATIC) -o $(OUTPUTAGENTDIR)/libAMDHSADebugAgent-$(ARCH_SUFFIX).so $(DBEHSAPATH) $(DBEHSALIBNAME)

.cpp.o:
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(OUTPUTAGENTDIR)/libAMDHSADebugAgent-$(ARCH_SUFFIX).so
	rm -f *.o
	rm -f $(DYNAMICLIBMODULEDIR)/HSADebuggerRTModule.o
	rm -f $(DYNAMICLIBMODULEDIR)/DynamicLibraryModule.o
	rm -f *.os
	rm -f *.d
