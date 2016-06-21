//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Functions to initialize the dispatch callbacks
//==============================================================================
#ifndef HSA_DEBUG_AGENT_H_
#define HSA_DEBUG_AGENT_H_

// Called from interception code set predispatch callbacks
HsailAgentStatus InitDispatchCallbacks(hsa_queue_t* queue);

// Called from interception code to shut down the AgentContext
void ShutDownHsaAgentContext(const bool skipDbeShutDown);

namespace HwDbgAgent
{
class AgentISABufferManager;

class AgentConfiguration;
}

HwDbgAgent::AgentISABufferManager* GetActiveISABufferManager();

HwDbgAgent::AgentConfiguration* GetActiveAgentConfig();

void RestoreSIGUSR2(void);

#endif // HSA_DEBUG_AGENT_H
