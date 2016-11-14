//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Interface for Agent logging,both for error and tracing purposes
//==============================================================================
#ifndef AGENT_LOGGING_H_
#define AGENT_LOGGING_H_

#include <sstream>

#include <hsa.h>

#include "AMDGPUDebug.h"
#include "CommunicationControl.h"

namespace HwDbgAgent
{
class AgentBinary;
}
// Always log errors
#define LOG_ERR_TO_STDERR 1

// Macro to create a stringstream, initialize it and use existing AgentLog()
#define AGENT_LOG(stream)           \
{                                   \
    std::stringstream buffer;       \
    buffer.str("");                 \
    buffer << stream << "\n";       \
    AgentLog(buffer.str().c_str()); \
}

// Macro to create a stringstream, initialize it and use existing AgentErrorLog()
#define AGENT_ERROR(stream)                 \
{                                           \
    std::stringstream buffer;               \
    buffer.str("");                         \
    buffer << stream << "\n";               \
    AgentErrorLog(buffer.str().c_str());    \
}

// Macro to create a stringstream, initialize it and use existing AgentWarningLog()
#define AGENT_WARNING(stream)               \
{                                           \
    std::stringstream buffer;               \
    buffer.str("");                         \
    buffer << stream << "\n";               \
    AgentWarningLog(buffer.str().c_str());  \
}

// Macro to create a stringstream, initialize it and use existing AgentOp()
#define AGENT_OP(stream)            \
{                                   \
    std::stringstream buffer;       \
    buffer.str("");                 \
    buffer << stream << "\n";       \
    AgentOP(buffer.str().c_str());  \
}

HsailAgentStatus AgentInitLogger();

HsailAgentStatus AgentCloseLogger();

void AgentLog(const char*);

void AgentLogLoadMap(const HsailSegmentDescriptor* pLoadedSegments,
                     const size_t                  numSegments);

void AgentLogPacketInfo(const HsailCommandPacket& incomingPacket);

void AgentLogSetFromConsole(const HsailLogCommand ipCommand);

// Agent side hsail-gdb OP to stdout.
void AgentOP(const char*);

// Print packet info to stdout
void AgentPrintPacketInfo(const HsailCommandPacket& incomingPacket);

void AgentErrorLog(const char*);

void AgentWarningLog(const char*);

void AgentLogSaveBinaryToFile(const HwDbgAgent::AgentBinary* pBinary, hsa_kernel_dispatch_packet_t*  pAqlPacket);

void AgentLogAQLPacket(const hsa_kernel_dispatch_packet_t*  pAqlPacket);

void AgentLogAppendFinalizerOptions(std::string& finalizerOptions);

#endif // AGENT_LOGGING_H_
