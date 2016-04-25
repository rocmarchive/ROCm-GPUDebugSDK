//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Class to print wave information
//==============================================================================
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstddef>

#include "AMDGPUDebug.h"

#include "AgentConfiguration.h"
#include "AgentLogging.h"
#include "AgentContext.h"
#include "AgentUtils.h"
#include "AgentWavePrinter.h"
#include "CommunicationControl.h"
#include "HSADebugAgent.h"

namespace HwDbgAgent
{
AgentDbgWavefront::AgentDbgWavefront(HwDbgCodeAddress wavefrontProgramCounter,
                                     HwDbgWavefrontAddress wavefrontAddress)
    : m_wavefrontProgramCounter(wavefrontProgramCounter),
      m_wavefrontAddress(wavefrontAddress)
{
    memset(m_workItemIds, -1, sizeof(int) * g_KERNEL_DEBUG_WORKITEMS_PER_WAVEFRONT * 3);
}

AgentDbgWavefront::~AgentDbgWavefront()
{

}

void AgentDbgWavefront::GetWorkItemCoordinate(int index, int coord[3]) const
{
    if ((-1 < index) && (g_KERNEL_DEBUG_WORKITEMS_PER_WAVEFRONT > index))
    {
        coord[0] = m_workItemIds[index * 3];
        coord[1] = m_workItemIds[index * 3 + 1];
        coord[2] = m_workItemIds[index * 3 + 2];
    }
    else
    {
        coord[0] = -1;
        coord[1] = -1;
        coord[2] = -1;
    }
}

bool AgentDbgWavefront::ContainsWorkItem(const int coord[3]) const
{
    bool retVal = false;

    for (int i = 0; i < g_KERNEL_DEBUG_WORKITEMS_PER_WAVEFRONT; i++)
    {
        // If we've reached the first undefined work item, stop:
        int currentItemCoord[3] = { -2, -2, -2};
        GetWorkItemCoordinate(i, currentItemCoord);

        if (-1 < currentItemCoord[0])
        {
            // Ignore undefined indices for y and z, as they are optional:
            if ((currentItemCoord[0] == coord[0]) &&
                ((0 > currentItemCoord[1]) || (0 > coord[1]) || (currentItemCoord[1] == coord[1])) &&
                ((0 > currentItemCoord[2]) || (0 > coord[2]) || (currentItemCoord[1] == coord[2])))
            {
                retVal = true;
                break;
            }
        }
        else // -1 >= currentItemCoord[0]
        {
            break;
        }
    }

    return retVal;
}

AgentWavePrinter::AgentWavePrinter():
        m_currentWavefronts(),
        m_DispatchGlobalWorkDimensions(-1), // State is unknown initially
        m_waveBufferShmKey(-1),
        m_waveBufferMaxSize(0)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    status = GetActiveAgentConfig()->GetConfigShmKey(HSAIL_DEBUG_CONFIG_WAVE_INFO_SHM, m_waveBufferShmKey);
    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not get shared mem key");
        return;
    }

    status = GetActiveAgentConfig()->GetConfigShmSize(HSAIL_DEBUG_CONFIG_WAVE_INFO_SHM, m_waveBufferMaxSize);
    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not get shared mem max size");
        return;
    }

    // We create the shared memory for the IPC
    InitializeWaveInfoShmem();

    AGENT_LOG("Initialize AgentWavePrinter");
}

void AgentWavePrinter::ClearCurrentWavefronts()
{
    m_currentWavefronts.clear();
}

HsailAgentStatus AgentWavePrinter::FreeWaveInfoShmem()
{
    AGENT_LOG("FreeWaveInfoShmem: Free shared memory buffer");

    HsailAgentStatus status;
    status = AgentFreeSharedMemBuffer(m_waveBufferShmKey, m_waveBufferMaxSize);

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("FreeWaveInfoShmem: Failed to free shared memory buffer");
    }

    return status;

}

void AgentWavePrinter::InitializeWaveInfoShmem()
{
    AGENT_LOG("InitializeWaveInfoShmem: Initialize wave info shared mem");

    HsailAgentStatus status;
    status = AgentAllocSharedMemBuffer(m_waveBufferShmKey, m_waveBufferMaxSize);

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("InitializeWaveInfoShmem: Could not initialize wave info shared mem");
    }

}

AgentWavePrinter::~AgentWavePrinter()
{
    FreeWaveInfoShmem();
}

HsailAgentStatus AgentWavePrinter::PrintActiveWaves(HwDbgEventType      dbeEventType,
                                                    HwDbgContextHandle  debugHandle)
{
    // We need to check that debugging has been started before
    // calling this function

    // Assert that we are in a post breakpoint state
    // Added in case this function is called in some strange manner
    // during expression evaluation
    assert(dbeEventType == HWDBG_EVENT_POST_BREAKPOINT);

    if (dbeEventType != HWDBG_EVENT_POST_BREAKPOINT)
    {
        AGENT_ERROR("PrintActiveWaves: DBE not in post breakpoint state");
        return HSAIL_AGENT_STATUS_FAILURE;
    }

    HwDbgStatus status;
    // Call the DBE
    // The pWaveInfo is basically the shadow TMA buffer that the DBE populates
    // The agent does not need to do anything to manage the pWaveInfo pointer
    const HwDbgWavefrontInfo* pWaveInfo = nullptr;
    uint32_t nWaves = 0;

    status = HwDbgGetActiveWavefronts(debugHandle, &pWaveInfo, &nWaves);
    bool isBufferEmpty = false;
    if (!AgentIsWaveInfoBufferValid(status, nWaves, pWaveInfo, isBufferEmpty))
    {
        AGENT_ERROR("PrintActiveWaves: WaveInfo buffer is invalid");
        return HSAIL_AGENT_STATUS_FAILURE;
    }
    HsailAgentStatus agentStatus = HSAIL_AGENT_STATUS_FAILURE;
    if (isBufferEmpty)
    {
        agentStatus  = HSAIL_AGENT_STATUS_SUCCESS;
    }
    else
    {
        agentStatus = PrintWaveInfoBuffer(nWaves, pWaveInfo);
    }


    return agentStatus;
}

// Not storing the parameters as members since we would always need to query the active waves
// from the DBE before we print
HsailAgentStatus AgentWavePrinter::PrintWaveInfoBuffer(int nWaves, const HwDbgWavefrontInfo* pWaveInfo)
{
    if (nWaves <= 0)
    {
        AGENT_LOG("Num Waves <= 0");
        return HSAIL_AGENT_STATUS_FAILURE;
    }

    if (pWaveInfo == nullptr && nWaves > 0)
    {
        AGENT_ERROR("Invalid pWaveInfo since nWaves is > 0");
        return HSAIL_AGENT_STATUS_FAILURE;
    }

    AGENT_OP("No of Waves " << nWaves);

    // Extremely simplistic for now,
    for (int i = 0; i < nWaves; i++)
    {
        // Check if wave's pc is a NOP
        AGENT_OP("Breakpoint at PC 0x" <<
                 std::hex << static_cast<unsigned int>(pWaveInfo->codeAddress));
    }

    return HSAIL_AGENT_STATUS_SUCCESS;
}

/// Needs a DBE context handle and a event type and sends the active wave info to gdb
HsailAgentStatus AgentWavePrinter::SendActiveWavesToGdb(HwDbgEventType      dbeEventType,
                                                        HwDbgContextHandle  debugHandle)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    // We need to check that debugging has been started before calling this function

    // Assert that we are in a post breakpoint state
    // Added in case this function is called in some strange manner
    // during expression evaluation
    assert(dbeEventType == HWDBG_EVENT_POST_BREAKPOINT);

    if (dbeEventType != HWDBG_EVENT_POST_BREAKPOINT)
    {
        AGENT_ERROR("SendActiveWavesToGdb: Called when DBE not in post breakpoint state");
        return status;
    }

    HwDbgStatus dbeStatus;

    // Call the DBE
    // The pWaveInfo is basically the shadow TMA buffer that the DBE populates
    // The agent does not need to do anything to manage the pWaveInfo pointer
    const HwDbgWavefrontInfo* pWaveInfo = nullptr;
    uint32_t nWaves = 0;

    dbeStatus = HwDbgGetActiveWavefronts(debugHandle, &pWaveInfo, &nWaves);

    bool isBufferEmpty = false;
    if (!AgentIsWaveInfoBufferValid(dbeStatus, nWaves, pWaveInfo, isBufferEmpty))
    {
        AGENT_ERROR("SendActiveWavesToGdb: WaveInfo buffer is invalid");
        return status;
    }

    // We still map and unmap the shared mem since we want to zero out the buffer
    void* pShm = AgentMapSharedMemBuffer(m_waveBufferShmKey, m_waveBufferMaxSize);

    if (pShm == (int*) - 1)
    {
        AGENT_ERROR("SendActiveWavesToGdb: Error mapping shared mem");
        status = HSAIL_AGENT_STATUS_FAILURE;
        return status;
    }

    // First zero the whole region out, we dont know how much of this region will be used
    // The next update to this logic may be smarter about what is written here
    memset(pShm, 0, m_waveBufferMaxSize);

    AGENT_LOG("No of active waves: " << nWaves );

    if (nWaves*sizeof(HsailAgentWaveInfo) > m_waveBufferMaxSize)
    {
        AGENT_LOG("Maximum number of waves possible in the wave info buffer "
                    << m_waveBufferMaxSize/sizeof(HsailAgentWaveInfo));

        AGENT_ERROR("Wave info buffer cannot hold all the active waves");
        return status;
    }

    for (size_t i = 0; i < nWaves; i++)
    {
        HsailAgentWaveInfo* pLocn = (HsailAgentWaveInfo*)pShm + i;
        pLocn->waveAddress = pWaveInfo[i].wavefrontAddress;
        pLocn->execMask = pWaveInfo[i].executionMask;
        pLocn->pc = pWaveInfo[i].codeAddress;

        pLocn->workGroupId.x = static_cast<uint32_t>(pWaveInfo[i].workGroupId.x);
        pLocn->workGroupId.y = static_cast<uint32_t>(pWaveInfo[i].workGroupId.y);
        pLocn->workGroupId.z = static_cast<uint32_t>(pWaveInfo[i].workGroupId.z);

        // We could just do a memcpy with 64*sizeof(uint32_t) but the loop is useful
        // to check if the type of the pWaveInfo in the DBE changes
        for (int j = 0; j < 64; j++)
        {
            pLocn->workItemId[j].x = static_cast<uint32_t>(pWaveInfo[i].workItemId[j].x);
            pLocn->workItemId[j].y = static_cast<uint32_t>(pWaveInfo[i].workItemId[j].y);
            pLocn->workItemId[j].z = static_cast<uint32_t>(pWaveInfo[i].workItemId[j].z);
        }
    }

    status = AgentUnMapSharedMemBuffer(pShm);
    return status;
}

} // End Namespace HwDbgAgent
