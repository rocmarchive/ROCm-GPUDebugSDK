//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file  AgentBreakpoint.cpp
/// \brief The breakpoint class
//==============================================================================
// Use some stl vectors for maintaining breakpoint handles
#include <algorithm>
#include <vector>

#include "AgentBreakpoint.h"
#include "AgentLogging.h"
#include "AgentUtils.h"

namespace HwDbgAgent
{

/// Construct an Agent breakpoint
AgentBreakpoint::AgentBreakpoint():
    m_bpState(HSAIL_BREAKPOINT_STATE_UNKNOWN),
    m_hitcount(0),
    m_GdbId(),
    m_pc(HSAIL_ISA_PC_UNKOWN),
    m_type(HSAIL_BREAKPOINT_TYPE_UNKNOWN),
    m_lineName("Unknown Line"),
    m_lineNum(-1),
    m_kernelName(""),
    m_condition(),
    m_handle(nullptr)
{

    AGENT_LOG("Allocate an AgentBreakpoint");
}

void AgentBreakpoint::PrintHitMessage() const
{
    if (m_bpState != HSAIL_BREAKPOINT_STATE_ENABLED &&
        m_bpState != HSAIL_BREAKPOINT_STATE_PENDING)
    {
        AGENT_ERROR("Printing called for a disabled breakpoint");
        // Don't return for now we want to see which breakpoint is causing this problem
    }

    std::stringstream buffer;
    buffer.str("");

    switch (m_type)
    {
        case HSAIL_BREAKPOINT_TYPE_TEMP_PC_BP:
            buffer  << "Temp Breakpoint: "
                    << "PC: " << std::hex << static_cast <unsigned long long>(m_pc)
                    << std::endl;
            break;

        case HSAIL_BREAKPOINT_TYPE_PC_BP:
            if (m_GdbId.empty())
            {
                if (m_lineNum != -1)
                {
                    buffer  << "Breakpoint: "
                            << " at line " << m_lineNum << std::endl;
                }
                else
                {
                    AGENT_ERROR("PrintHitMessage: We should have line number if not source info");

                }
            }
            else
            {
                if (m_lineNum != -1)
                {
                    buffer  << "Breakpoint " << m_GdbId.at(0)
                            << " at " << m_lineName << " temp_source@line " << m_lineNum << std::endl;
                }
                else
                {
                    /// \todo: investigate why do we hit this case when stopping at a source breakpoint
                    buffer  << "Breakpoint " << m_GdbId.at(0)
                            << " at " << m_lineName << std::endl;
                }
            }

            break;

        default:
            buffer  << "Unsupported Breakpoint Type" << std::endl;
            break;

    }

    AgentOP(buffer.str().c_str());

    return;
}

// Find an empty slot in the payload, and save the hitcount and gdb id
HsailAgentStatus AgentBreakpoint::UpdateNotificationPayload(HsailNotificationPayload* pNotify) const
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    if (pNotify == nullptr)
    {
        AGENT_ERROR("UpdateNotificationPayload: Notification structure is nullptr");
        return status;

    }

    if (pNotify->m_Notification != HSAIL_NOTIFY_BREAKPOINT_HIT)
    {
        AGENT_ERROR("UpdateNotificationPayload: Invalid type, expect breakpoint hit payload");
        return status;
    }

    bool posFound = false;
    unsigned int pos = 0;

    for (pos = 0; pos < HSAIL_MAX_REPORTABLE_BREAKPOINTS; pos++)
    {
        // If this is an empty slot or my slot, that'll work
        if (pNotify->payload.BreakpointHit.m_breakpointId[pos] == -1 ||
            pNotify->payload.BreakpointHit.m_breakpointId[pos] == m_GdbId.at(0))
        {
            posFound = true;
            break;
        }
    }

    if (posFound)
    {
        pNotify->payload.BreakpointHit.m_breakpointId[pos] = m_GdbId.at(0);
        pNotify->payload.BreakpointHit.m_hitCount[pos] = m_hitcount;
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }
    else
    {
        AGENT_ERROR("An empty slot could not be found in the Notification payload");
        status = HSAIL_AGENT_STATUS_FAILURE;
    }

    return status;
}

HsailAgentStatus AgentBreakpoint::CreateBreakpointDBE(const HwDbgContextHandle dbeHandle,
                                                      const GdbBkptId          gdbID)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    if (m_type != HSAIL_BREAKPOINT_TYPE_PC_BP)
    {
        AGENT_ERROR("CreateBreakpointDBE: This breakpoint was marked as some other type");
        return status;
    }

    if (dbeHandle == nullptr )
    {
        AGENT_ERROR("CreateBreakpointDBE: Invalid dbeHandle ");
        return status;
    }

    if (m_pc == HSAIL_ISA_PC_UNKOWN)
    {
        AGENT_ERROR("CreateBreakpointDBE: Invalid PC ");
        return status;

    }
    // If no other GDB ID existed or the ID was not specified, we need to create in DB
    bool isBreakpointNeededInDBE = m_GdbId.empty() || gdbID == g_UNKOWN_GDB_BKPT_ID ;

    // We need to enqueue  a GDB id for this breakpoint
    if (gdbID != g_UNKOWN_GDB_BKPT_ID)
    {
        std::vector<GdbBkptId>::iterator it;
        it = find (m_GdbId.begin(), m_GdbId.end(), gdbID);
        if (it == m_GdbId.end())
        {
            m_GdbId.push_back(gdbID);
        }
    }

    if (m_handle != nullptr)
    {
        // Get the location where the breakpoint presently is
        HwDbgCodeAddress presentPC;
        HwDbgStatus dbeStatus = HwDbgGetCodeBreakpointAddress(dbeHandle, m_handle, &presentPC);
        if (dbeStatus == HWDBG_STATUS_SUCCESS &&
            presentPC != m_pc)
        {
            isBreakpointNeededInDBE = true;
        }
    }

    if (isBreakpointNeededInDBE)
    {
        // This breakpoint should be in state enabled (if created successfully)
        HwDbgStatus dbeStatus = HwDbgCreateCodeBreakpoint(dbeHandle, m_pc, &m_handle);

        if (dbeStatus != HWDBG_STATUS_SUCCESS)
        {
            // This is piped to the Log instead of stderr since for momentary breakpoints
            // we presently calll the DBE with invalid values
            AGENT_LOG("CreateBreakpointDBE: Error from DBE HwDbgCreateCodeBreakpoint" <<
                      GetDBEStatusString(dbeStatus));

            // \todo, FB 11231, we need to change how we call the DBE
            m_bpState = HSAIL_BREAKPOINT_STATE_PENDING;
            m_handle = nullptr;
            status = HSAIL_AGENT_STATUS_SUCCESS;
        }
        else
        {
            // Breakpoint successfully enabled in DBE,
            m_bpState = HSAIL_BREAKPOINT_STATE_ENABLED;
            status = HSAIL_AGENT_STATUS_SUCCESS;
        }
    }
    else
    {
        AGENT_LOG("CreateBreakpointDBE: Did not create breakpoint in DBE since it was already created");
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }

    return status;
}


HsailAgentStatus AgentBreakpoint::DeleteBreakpointDBE(const HwDbgContextHandle dbeHandle,
                                                      const GdbBkptId          gdbID)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    if (dbeHandle == nullptr)
    {
        AGENT_ERROR("DeleteBreakpointDBE: Invalid DBE context handle");
        return status;
    }

    if (gdbID != g_UNKOWN_GDB_BKPT_ID)
    {
        if (!m_GdbId.empty())
        {
            unsigned int i;
            bool isgdbIDFoud  = false;

            for (i = 0; i < m_GdbId.size(); i++)
            {
                if (m_GdbId.at(i) == gdbID)
                {
                    AGENT_LOG("DeleteBreakpointDBE: GDB ID " << gdbID << " found") ;

                    isgdbIDFoud = true;
                    break;

                }
            }

            if (isgdbIDFoud)
            {
                m_GdbId.erase(m_GdbId.begin() + i);
                AGENT_LOG("DeleteBreakpointDBE: Remove element " << i  <<
                          "\t New GDB ID Vector Len = " << m_GdbId.size()) ;
            }
            else
            {
                AGENT_ERROR("DeleteBreakpointDBE: Could not find the GDB ID in this vector");
            }
        }
    }

    bool isBreakpointDeletedInDBE = m_GdbId.empty() || gdbID == g_UNKOWN_GDB_BKPT_ID ;

    if (isBreakpointDeletedInDBE)
    {
        // We only need to do this if the handle is not nullptr
        if (m_handle != nullptr)
        {
            // This breakpoint should be in state disabled
            HwDbgStatus dbeStatus = HwDbgDeleteCodeBreakpoint(dbeHandle, m_handle);

            if (dbeStatus != HWDBG_STATUS_SUCCESS)
            {
                AGENT_LOG("DeleteBreakpointDBE: Error from DBE HwDbgDeleteBreakpoint " <<
                          GetDBEStatusString(dbeStatus));
                //status = HSAIL_AGENT_STATUS_FAILURE;
                status = HSAIL_AGENT_STATUS_SUCCESS;
            }
            else
            {
                // Breakpoint is disabled in DBE,
                m_handle = nullptr;
                m_bpState = HSAIL_BREAKPOINT_STATE_DISABLED;
                status = HSAIL_AGENT_STATUS_SUCCESS;
            }
        }
        else
        {
            status = HSAIL_AGENT_STATUS_SUCCESS;
        }
    }
    else
    {
        AGENT_LOG("DeleteBreakpointDBE: Did not delete breakpoint in DBE since duplicates exist");
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }

    return status;
}

HsailAgentStatus AgentBreakpoint::DeleteBreakpointKernelName(const GdbBkptId gdbID)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_SUCCESS;
    if (m_type != HSAIL_BREAKPOINT_TYPE_KERNEL_NAME_BP)
    {
        AGENT_ERROR("This function should only be called with kernel name breakpoints");
        return status;
    }

    if (gdbID != g_UNKOWN_GDB_BKPT_ID)
    {
        if (!m_GdbId.empty())
        {
            unsigned int i;
            bool isgdbIDFoud  = false;

            for (i = 0; i < m_GdbId.size(); i++)
            {
                if (m_GdbId.at(i) == gdbID)
                {
                    AGENT_LOG("DeleteBreakpointDBE: GDB ID " << gdbID << " found") ;

                    isgdbIDFoud = true;
                    break;

                }
            }

            if (isgdbIDFoud)
            {
                m_GdbId.erase(m_GdbId.begin() + i);
                AGENT_LOG("DeleteBreakpointDBE: Remove element " << i  <<
                          "\t GDB ID Vector Len = " << m_GdbId.size()) ;
            }
            else
            {
                AGENT_LOG("DeleteBreakpointKernelName: Could not find the GDB ID in this vector");
            }
        }
    }

    return status;
}

AgentBreakpointCondition::AgentBreakpointCondition():
    m_workitemID(gs_UNKNOWN_HWDBGDIM3),
    m_workgroupID(gs_UNKNOWN_HWDBGDIM3),
    m_conditionCode(HSAIL_BREAKPOINT_CONDITION_ANY)
{
    AGENT_LOG("Allocate an AgentBreakpointCondition");
}

HsailAgentStatus AgentBreakpointCondition::CheckCondition(const HwDbgWavefrontInfo* pWaveInfo,
                                                                bool&               isValidConditionOut,
                                                                HsailConditionCode& conditionCodeOut) const
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    if (pWaveInfo == nullptr)
    {
        AGENT_ERROR("CheckCondition: WaveInfo is nullptr");
        isValidConditionOut = false;
        return status;
    }

    bool outputCode = false;
    conditionCodeOut = m_conditionCode;

    switch (m_conditionCode)
    {
        case HSAIL_BREAKPOINT_CONDITION_EQUAL:
        {
            outputCode = AgentIsWorkItemPresentInWave(m_workgroupID, m_workitemID, pWaveInfo);
            status = HSAIL_AGENT_STATUS_SUCCESS;
            break;
        }

        case HSAIL_BREAKPOINT_CONDITION_ANY:
        {
            outputCode = true;
            status = HSAIL_AGENT_STATUS_SUCCESS;
            break;
        }

        case HSAIL_BREAKPOINT_CONDITION_UNKNOWN:
        {
            outputCode = false;
            break;
        }

        default:
            AGENT_ERROR("Condition code saved is invalid");
    }

    isValidConditionOut = outputCode;
    return status;
}

HsailAgentStatus AgentBreakpointCondition::CheckCondition(const HwDbgDim3 ipWorkGroup, const HwDbgDim3 ipWorkItem, bool& conditionCodeOut) const
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    bool outputCode = false;

    switch (m_conditionCode)
    {
        case HSAIL_BREAKPOINT_CONDITION_EQUAL:
        {
            outputCode = CompareHwDbgDim3(ipWorkGroup, m_workgroupID) &&
                         CompareHwDbgDim3(ipWorkItem, m_workitemID);
            status = HSAIL_AGENT_STATUS_SUCCESS;
            break;
        }

        case HSAIL_BREAKPOINT_CONDITION_ANY:
        {
            outputCode = true;
            status = HSAIL_AGENT_STATUS_SUCCESS;
            break;
        }

        case HSAIL_BREAKPOINT_CONDITION_UNKNOWN:
        {
            outputCode = false;
            break;
        }

        default:
            AGENT_ERROR("Condition code saved is invalid");
    }

    conditionCodeOut = outputCode;
    return status;
}

HwDbgDim3 AgentBreakpointCondition::GetWG() const
{
    return m_workgroupID;
}

HwDbgDim3 AgentBreakpointCondition::GetWI() const
{
    return m_workitemID;
}

void AgentBreakpointCondition::PrintCondition() const
{
    switch (m_conditionCode)
    {
        case HSAIL_BREAKPOINT_CONDITION_EQUAL:
        {
            AGENT_OP("Condition: active work-group: " <<
                     m_workgroupID.x << ", " << m_workgroupID.y << ", " << m_workgroupID.z <<
                     " @ work-item: " <<
                     m_workitemID.x << ", " << m_workitemID.y << ", " << m_workitemID.z);
            break;
        }

        case HSAIL_BREAKPOINT_CONDITION_ANY:
        {
            break;
        }

        default:
            AGENT_ERROR("PrintCondition: Condition code not supported");

    }
}

HsailAgentStatus AgentBreakpointCondition::SetCondition(const HsailConditionPacket& ipCondition)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    if (ipCondition.m_conditionCode != HSAIL_BREAKPOINT_CONDITION_UNKNOWN)
    {
        // Read in condition
        m_conditionCode = ipCondition.m_conditionCode;

        m_workgroupID.x = ipCondition.m_workgroupID.x;
        m_workgroupID.y = ipCondition.m_workgroupID.y;
        m_workgroupID.z = ipCondition.m_workgroupID.z;

        m_workitemID.x = ipCondition.m_workitemID.x;
        m_workitemID.y = ipCondition.m_workitemID.y;
        m_workitemID.z = ipCondition.m_workitemID.z;

        AGENT_LOG("Set Condition: Workgroup: " <<
                  m_workgroupID.x << ", " << m_workgroupID.y << ", " << m_workgroupID.z << "\t" <<
                  "WorkItem: " <<
                  m_workitemID.x << ", " << m_workitemID.y << ", " << m_workitemID.z);

        status = HSAIL_AGENT_STATUS_SUCCESS;
    }

    return status;
}

}
