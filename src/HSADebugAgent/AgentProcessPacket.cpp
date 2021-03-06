//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Agent processing, functions to consume FIFO packets and configure expression evaluation
//==============================================================================
#include <cstdbool>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <sstream>
#include <stdio.h>

#include "hsa.h"

// Agent includes
#include "AgentBreakpointManager.h"
#include "AgentContext.h"
#include "AgentFocusWaveControl.h"
#include "AgentLogging.h"
#include "AgentProcessPacket.h"
#include "CommunicationControl.h"

// Add DBE (Version decided by Makefile)
#include "AMDGPUDebug.h"

static void DBEDeleteBreakpoint(HwDbgAgent::AgentContext* pActiveContext,
                                const HsailCommandPacket& ipPacket)
{

    if (!pActiveContext->HasHwDebugStarted())
    {
        AgentErrorLog("DBEDeleteBreakpoint:BeginDebugging has not occured\n");
    }
    else
    {

        HwDbgAgent::AgentBreakpointManager* pBpManager = pActiveContext->GetBpManager();

        HsailAgentStatus status;

        status = pBpManager->DeleteBreakpoint(pActiveContext->GetActiveHwDebugContext(),
                                              ipPacket);

        if (status != HSAIL_AGENT_STATUS_SUCCESS)
        {
            AgentErrorLog("DBEDeleteBreakpoint: Could not delete breakpoint\n");
        }

    }

    return;
}



static void CreateBreakpointPacket(      HwDbgAgent::AgentContext* pActiveContext,
                                   const HsailCommandPacket&       ipPacket)
{

    HwDbgAgent::AgentBreakpointManager* pBpManager = pActiveContext->GetBpManager();
    HwDbgAgent::HsailBkptType bpType = HwDbgAgent::HSAIL_BREAKPOINT_TYPE_PC_BP;

    if ((char)0 != ipPacket.m_kernelName[0])
    {
        bpType = HwDbgAgent::HSAIL_BREAKPOINT_TYPE_KERNEL_NAME_BP;
    }

    // Get Loaded Agent
    HsailAgentStatus status;
    status = pBpManager->CreateBreakpoint(pActiveContext->GetActiveHwDebugContext(),
                                          pActiveContext->GetDispatchedAQLPacket(),
                                          ipPacket,
                                          bpType);

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AgentErrorLog("DBECreateBreakpoint: Could not create a breakpoint\n");
    }

}

static void DBEDisablePCBreakpoint(HwDbgAgent::AgentContext* pActiveContext,
                                   const HsailCommandPacket& ipPacket)
{
    if (!pActiveContext->HasHwDebugStarted())
    {
        AgentErrorLog("DBEDisablePCBreakpoint:should not Disable breakpoint without BeginDebugging\n");
    }
    else
    {
        HwDbgAgent::AgentBreakpointManager* pBpManager = pActiveContext->GetBpManager();

        HsailAgentStatus status;
        status = pBpManager->DisablePCBreakpoint(pActiveContext->GetActiveHwDebugContext(),
                                                 ipPacket);

        if (status != HSAIL_AGENT_STATUS_SUCCESS)
        {
            AgentErrorLog("DBEDisablePCBreakpoint: Could not disable a breakpoint\n");
        }
    }

    return;
}

static void DBEEnablePCBreakpoint(HwDbgAgent::AgentContext* pActiveContext,
                                  const HsailCommandPacket& ipPacket)
{
    if (!pActiveContext->HasHwDebugStarted())
    {
        AgentErrorLog("DBE: We should not Enable breakpoint without BeginDebugging \n");
    }
    else
    {
        HwDbgAgent::AgentBreakpointManager* pBpManager = pActiveContext->GetBpManager();

        HsailAgentStatus status;
        status = pBpManager->EnablePCBreakpoint(pActiveContext->GetActiveHwDebugContext(),
                                                ipPacket);

        if (status != HSAIL_AGENT_STATUS_SUCCESS)
        {
            AgentErrorLog("DBEEnablePCBreakpoint: Could not enable a breakpoint\n");
        }
    }

    return;
}

static void DBEMomentaryBreakpoint(HwDbgAgent::AgentContext* pActiveContext,
                                   const HsailCommandPacket& ipPacket)
{
    HwDbgAgent::AgentBreakpointManager* pBpManager = pActiveContext->GetBpManager();

    HsailAgentStatus status;
    status = pBpManager->CreateMomentaryBreakpoints(pActiveContext->GetActiveHwDebugContext(),
                                                    ipPacket);

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AgentErrorLog("DBEMomentaryBreakpoint: Could not create a momentary breakpoint\n");
    }
}

// Global pointer to active context used for the expression evaluator
// Can be fixed soon by checking a static variable in the function
HwDbgAgent::AgentContext* g_ActiveContext = nullptr;

void SetEvaluatorActiveContext(HwDbgAgent::AgentContext* activeContext)
{
    AGENT_LOG("SetEvaluatorActiveContext: Active Context Pointer: " << activeContext);

    g_ActiveContext = activeContext;
}

// Global pointer to kernel parameters buffers used for the var eval
// isaMemoryRegion type
void* g_KernelParametersBuffer = nullptr;

void SetKernelParametersBuffers(const hsa_kernel_dispatch_packet_t* pAqlPacket)
{
    if (nullptr == pAqlPacket)
    {
        g_KernelParametersBuffer = nullptr;
    }
    else
    {
        g_KernelParametersBuffer = (void*)pAqlPacket->kernarg_address;
    }
}

void KillHsailDebug(bool isQuitIssued)
{
    AGENT_LOG("KillHsailDebug: isQuitIssued: " << isQuitIssued);

    HsailAgentStatus status = g_ActiveContext->KillDispatch();

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("KillDispatch: Killing the dispatch by expression evaluation");
    }

    // Force cleanup in EndDebugging since the dispatch is not yet complete
    status = g_ActiveContext->EndDebugging(true);

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("KillDispatch: Ending debugging from within expression evaluation");
    }

    AGENT_LOG("Exit KillHsailDebug, status: " << status);
}

bool GetPrivateMemory(HwDbgDim3 workGroup, HwDbgDim3 workItem, size_t base, size_t offset, size_t numByteToRead, void* pMemOut, size_t* pNumBytesOut)
{
    bool retVal = false;

    HwDbgDim3 workGroupId;
    workGroupId.x = workGroup.x;
    workGroupId.y = workGroup.y;
    workGroupId.z = workGroup.z;

    HwDbgDim3 workItemId;
    workItemId.x = workItem.x;
    workItemId.y = workItem.y;
    workItemId.z = workItem.z;

    HwDbgStatus status = HWDBG_STATUS_SUCCESS;

    if (g_ActiveContext == nullptr)
    {
        AgentErrorLog("GetPrivateMemory: Active context is nullptr\n");
        (*(int*)pMemOut) = 0;
        return false;
    }

    AGENT_LOG("Entering GetPrivateMemory:  work-group ("
              << workGroup.x << "," << workGroup.y << "," << workGroup.z
              << ") and work-item ("
              << workItem.x << "," << workItem.y << "," << workItem.z
              << ")");

    AGENT_LOG("GetPrivateMemory: " <<
              "base: "             << base << " " <<
              "offset: "           << offset << " " <<
              "numByteToRead: "    << numByteToRead << " " <<
              "pMemOut: "          << pMemOut << " " <<
              "pNumBytesOut: "     << pNumBytesOut);


    /*    char buffer[256];
        sprintf(buffer,"GPM WG:%d,%d,%d WI:%d,%d,%d realLocation:%ld\n", workGroup.x, workGroup.y, workGroup.z, workItem.x, workItem.y, workItem.z, (size_t)pMemOut);
        AgentLog(buffer);

        sprintf(buffer,"GPM AC:%x, DX:%x, base:%ld, base+offset:%ld, bytes to read:%ld\n", g_ActiveContext, (void*)g_ActiveContext->GetActiveHwDebugContext(), base, base+offset, numByteToRead);
        AgentLog(buffer);

        sprintf(buffer,"Address:%x\n", pMemOut);
        AgentLog(buffer);*/

    status = HwDbgReadMemory(g_ActiveContext->GetActiveHwDebugContext(),
                             1 /* IMR_Scratch */,
                             workGroupId, workItemId,
                             base + offset,
                             numByteToRead,
                             pMemOut,
                             pNumBytesOut);

    if (status != HWDBG_STATUS_SUCCESS)
    {
        AGENT_ERROR("GetPrivateMemory: Error in HwDbgReadMemory, "<< GetDBEStatusString(status));
    }
    else
    {
        retVal = true;
    }

    AGENT_LOG("Exit GetPrivateMemory, return code: " << retVal);

    return retVal;
}

enum LocationRegister
{
    LOC_REG_REGISTER,   ///< A register holds the value
    LOC_REG_STACK,      ///< The frame pointer holds the value
    LOC_REG_NONE,       ///< No registers are to be used in getting the value
    LOC_REG_UNINIT,     ///< Default / max value
};

// This function is called by the expression evaluator
void SetHsailThreadCmdInfo(unsigned int wgX, unsigned int wgY, unsigned int wgZ,
                           unsigned int wiX, unsigned int wiY, unsigned int wiZ)
{
    char buffer[256];
    HwDbgDim3 focusWg;
    focusWg.x = wgX;
    focusWg.y = wgY;
    focusWg.z = wgZ;

    HwDbgDim3 focusWI;
    focusWI.x = wiX;
    focusWI.y = wiY;
    focusWI.z = wiZ;

    HsailAgentStatus status = g_ActiveContext->m_pFocusWaveControl->SetFocusWave(nullptr, &focusWg, &focusWI);

    sprintf(buffer, "SetHsailThreadCmdInfo: got here wg:%d %d %d, wi:%d %d %d \n",
            focusWg.x, focusWg.y, focusWg.z,
            focusWI.x, focusWI.y, focusWI.z);

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AgentErrorLog("Could not change focus wave from GDB command\n");
        AgentErrorLog(buffer);
    }

    AgentLog(buffer);
}

// Used for cases where we want to make GDB parse a valid expression to populate
// GDB's language specific structures
void RunExpressionEval()
{
    AGENT_LOG("Do nothing expression evaluation");
}


void* gVariableValueForRelease = nullptr;
void FreeVarValue(void)
{
    free(gVariableValueForRelease);
    gVariableValueForRelease = nullptr;
}

void* GetVarValue(unsigned int reg_type, size_t var_size, unsigned int reg_num, bool deref_value, unsigned int offset, unsigned int resource, unsigned int isa_memory_region, unsigned int piece_offset, unsigned int piece_size, int const_add)
{
    AGENT_LOG("Entering GetVarValue:" << "\n\t" <<
                "reg_type "         << reg_type         << "\n\t" <<
                "var_size "         << var_size         << "\n\t" <<
                "reg_num "          << reg_num          << "\n\t" <<
                "deref_value "      << deref_value      << "\n\t" <<
                "offset "           << offset           << "\n\t" <<
                "resource "         << resource         << "\n\t" <<
                "isa_memory_region "<< isa_memory_region<< "\n\t" <<
                "piece_offset "     << piece_offset     << "\n\t" <<
                "piece_size "       << piece_size       << "\n\t" <<
                "const_add "        << const_add );

    // Results
    /* prepare the largest primitive buffer but the GetPrivateMemory will get the var_size */
    void* variableValues = malloc(8);
    memset(variableValues, 0, 8);

    // unsigned int valueStride = 0;

    // 1. Get the base location:
    static const size_t zero = 0;
    const void* loc = nullptr;
    //  size_t locStride = 0;

    /* offset for step 2 */
    size_t totalOffset = 0;

    /* gdb later turns this to nullptr so we need a copy for releasing this */
    gVariableValueForRelease = variableValues;

    if (nullptr == variableValues)
    {
        return nullptr;
    }

    switch (reg_type)
    {
        case LOC_REG_REGISTER:
        {
        }
        break;

        case LOC_REG_STACK:
        {
        }
        break;

        case LOC_REG_NONE:
        {
            loc = &zero;
        }
        break;

        case LOC_REG_UNINIT:
        {
            // This is currently the information for some unsupported locations, (e.g. __local T* parameters):
            return nullptr;
        }
        break;

        default:
        {
            AGENT_LOG("hsail-printf unsupported reg type");
        }
        break;
    }

    /* 2. Dereference and apply offset as needed: */
    /* currently ignoring array offset */
    totalOffset = offset;

    if (deref_value)
    {
        // Note: This assumes for dereferenced types that the values of the base pointer are all the same.
        // A more "correct" method would be to iterate all the active work items, get the value for each,
        // then copy that into a buffer.

        size_t realLocation = *((size_t*)loc) + totalOffset + piece_offset;

        // Since we applied the piece offset here (to get the correct value), we can reset the piece offset we will use to parse to 0:
        piece_offset = 0;
	AGENT_LOG("Access Memory Region " << isa_memory_region);
        switch (isa_memory_region)
        {

            case 0: // = IMR_Global
            {
                // Global Memory:
                memcpy(variableValues, ((void*)realLocation), var_size);
                // valueStride = (unsigned int)var_size;
            }
            break;

            case 1: // = IMR_Scratch
            {
                // Private memory:
                size_t locVarSize;
                /* for some reason only in the first time allocation if I do not erase the variableValues than the data that is received from
                   GetPrivateMemory is faulty. freeing and allocating the memory solves this problem. This a temporary solution */
                HwDbgDim3 focusWg;
                HwDbgDim3 focusWi;
                HsailAgentStatus status = g_ActiveContext->m_pFocusWaveControl->GetFocus(focusWg, focusWi);

                if (status != HSAIL_AGENT_STATUS_SUCCESS)
                {
                    AGENT_ERROR("Could not get focus parameters");
                }

                bool rc = GetPrivateMemory(focusWg, focusWi,
                                           (size_t)realLocation, 0, var_size, variableValues, &locVarSize);

                if (rc)
                {
                    // Only signify success if we actually asked for and got a value from the DBE:
                    /* valueStride = (unsigned int)locVarSize; return value is used in original ref function */
                }
            }
            break;

            case 2: // = IMR_Group
            {
                // Local memory:
                // Not currently supported
                return nullptr;
            }
            break;

            case 3: // = IMR_ExtUserData
            {
                // Uri, 21/05/13 - As a workaround to getting an ExtUserData (32-bit aligned OpenCL arguments buffer) pointer
                // And having to read from the AQL (64-bit aligned HSA arguments buffer), we need to double the offset here,
                // As this works properly for most parameters.
                // Should be revised (as parameters larger than 32-bit will not work) or removed when the compiler moves to AQL offsets.
                realLocation *= 2;
            }

            case 4: // = IMR_AQL
            case 5: // = IMR_FuncArg
            {
                // assume kernel argument is not nullptr but value is 0?
                // Kernel arguments:
                // Add the offset to the kernel args base pointer:
                realLocation += (size_t)g_KernelParametersBuffer;
                memcpy(variableValues, ((void*)realLocation), var_size);
                /* valueStride = (unsigned int)var_size; return value is used in original ref function */

            }
            break;

            default:
            {
                AGENT_ERROR("Unsupported Memory Region" << isa_memory_region);
            }
        }
    }
    else
    {
        /* valueStride = 0;  return value is used in original ref function */
        variableValues = (void*)((size_t)loc + totalOffset);
    }

    variableValues = (void*)((size_t)variableValues + piece_offset);

    AGENT_LOG("Exit GetVarValue");

    return (void*)(*(size_t*)variableValues);
}

void AgentProcessPacket(HwDbgAgent::AgentContext* pActiveContext,
                        const HsailCommandPacket& packet)
{
    switch (packet.m_command)
    {
        // \todo What is the earliest time that this packet can be sent  ?
        // Is it theoretically possible for GDB to send this packet ?
        // This packet is not needed since we do the setup in the predispatch
        case HSAIL_COMMAND_BEGIN_DEBUGGING:
            pActiveContext->PrintDBEVersion();
            AgentErrorLog("Unsupported command packet error");
            break;

        case HSAIL_COMMAND_CREATE_BREAKPOINT:
            CreateBreakpointPacket(pActiveContext, packet);
            break;

        case HSAIL_COMMAND_DISABLE_BREAKPOINT:
            DBEDisablePCBreakpoint(pActiveContext, packet);
            break;

        case HSAIL_COMMAND_DELETE_BREAKPOINT:
            DBEDeleteBreakpoint(pActiveContext, packet);
            break;

        case HSAIL_COMMAND_MOMENTARY_BREAKPOINT:
            DBEMomentaryBreakpoint(pActiveContext, packet);
            break;

        case HSAIL_COMMAND_CONTINUE:
            pActiveContext->m_ReadyToContinue = true;
            break;

        case HSAIL_COMMAND_ENABLE_BREAKPOINT:
            DBEEnablePCBreakpoint(pActiveContext, packet);
            break;

        case HSAIL_COMMAND_SET_LOGGING:
            AgentLogSetFromConsole(packet.m_loggingInfo);
            break;

        case HSAIL_COMMAND_UNKNOWN:
            pActiveContext->PrintDBEVersion();
            AgentErrorLog("Incomplete command packet error");
            break;

        default:
            pActiveContext->PrintDBEVersion();
            AgentErrorLog("Incomplete command packet error");
            break;
    }
}
