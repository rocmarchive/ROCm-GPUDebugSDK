//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file  AgentBreakpoint.h
/// \brief Agent breakpoint structure
//==============================================================================

#ifndef AGENT_BREAKPOINT_H_
#define AGENT_BREAKPOINT_H_

#include <string>

#include "AMDGPUDebug.h"
#include "CommunicationControl.h"

namespace HwDbgAgent
{
/// GDB assigns a integer breakpoint id to each breakpoint
typedef int GdbBkptId;

const GdbBkptId g_UNKOWN_GDB_BKPT_ID = -9999;

typedef enum
{
    HSAIL_BREAKPOINT_STATE_UNKNOWN,   ///< We havent set breakpoint state yet
    HSAIL_BREAKPOINT_STATE_PENDING,   ///< HSAIL breakpoint received and not yet been created
    HSAIL_BREAKPOINT_STATE_DISABLED,  ///< HSAIL Breakpoint has been created but is disabled
    HSAIL_BREAKPOINT_STATE_ENABLED,   ///< HSAIL Breakpoint has been created and is enabled
} HsailBkptState;

typedef enum
{
    HSAIL_BREAKPOINT_TYPE_UNKNOWN,      ///< We havent set breakpoint type yet
    HSAIL_BREAKPOINT_TYPE_TEMP_PC_BP,   ///< This is a temp PC breakpoint
    HSAIL_BREAKPOINT_TYPE_PC_BP,        ///< A program counter breakpoint
    HSAIL_BREAKPOINT_TYPE_DATA_BP,      ///< A data breakpoint
    HSAIL_BREAKPOINT_TYPE_KERNEL_NAME_BP,///< A kernel name breakpoint
} HsailBkptType;


/// A single condition for a breakpoint.
/// This class is stored within a AgentBreakpoint.
/// The condition is evaluated by calling CheckCondition() calls
class AgentBreakpointCondition
{
public:
    AgentBreakpointCondition();

    ~AgentBreakpointCondition()
    {
    }

    /// Populate from the condition packet we get from GDB
    /// \param[in] pCondition The condition packet we get from GDB
    /// \return HSAIL agent status
    HsailAgentStatus SetCondition(const HsailConditionPacket& condition);

    /// Check the condition against a workgroup and workitem pair
    /// \param[in]  ipWorkGroup         The workgroup ID to compare condition with
    /// \param[in]  ipWorkItem          The workitem ID to compare condition with
    /// \param[out] conditionCodeOut    The output condition
    /// \return HSAIL agent status
    HsailAgentStatus CheckCondition(const HwDbgDim3 ipWorkGroup, const HwDbgDim3 ipWorkItem, bool& conditionCodeOut) const;

    /// Check the condition against a wavefront
    /// The checkCondition function can be implemented in multiple forms, with the SIMT model
    ///
    /// \param[in] pWaveInfo         An entry from the waveinfo buffer
    /// \param[out] conditionCodeOut Return true if the condition matches
    /// \param[out] conditionTypeOut The type of condition.  Based on this type the focus control changes focus
    /// \return HSAIL agent status
    HsailAgentStatus CheckCondition(const HwDbgWavefrontInfo* pWaveInfo,
                                          bool&               isValidConditionOut,
                                          HsailConditionCode& conditionCodeOut) const;

    /// Get function to get the workgroup, used for FocusControl
    /// \return The work group used for this condition
    HwDbgDim3 GetWG() const;

    /// Get function to get the workitem, used for FocusControl
    /// \return The work item used for this condition
    HwDbgDim3 GetWI() const;

    /// Print the condition to the AGENT_OP
    void PrintCondition() const;

private:

    /// Disable copy constructor
    AgentBreakpointCondition(const AgentBreakpointCondition&);

    /// Disable assignment operator
    AgentBreakpointCondition& operator=(const AgentBreakpointCondition&);

    /// The work group used to match this condition
    HwDbgDim3 m_workitemID;

    /// The work item used to match this condition
    HwDbgDim3 m_workgroupID;

    /// The type of condition
    HsailConditionCode m_conditionCode;

};


/// A single HSAIL breakpoint, includes the GDB::DBE handle information
class AgentBreakpoint
{
public:

    /// The present state of the breakpoint
    /// \todo make this private since it should only be changed by the DBE functions
    HsailBkptState m_bpState;

    /// Number of times the BP was hit (unit reported in: wavefronts)
    int m_hitcount;

    /// The GDB IDs that map to this PC
    std::vector<GdbBkptId> m_GdbId;

    /// The PC we set the breakpoint on
    HwDbgCodeAddress m_pc;

    /// The type of the breakpoint
    HsailBkptType m_type;

    /// The line message that will be printed
    /// The constant g_HSAIL_BKPT_MESSAGE_LEN will limit the length of this string since
    /// it is sent within a packet from GDB
    std::string m_lineName;

    /// The HSAIL source line number
    int m_lineNum;

    /// Kernel name - used for function breakpoints
    std::string m_kernelName;

    /// The condition that will be checked for this breakpoint.
    /// Presently hsail-gdb supports only one condition at a time per breakable line
    AgentBreakpointCondition m_condition;

    /// Construct an Agent breakpoint
    AgentBreakpoint();

    /// Update an empty slot in the notification payload with the AgentBreakpoint's
    /// information such as hitcount.
    /// \param[in] payload The notification payload
    HsailAgentStatus UpdateNotificationPayload(HsailNotificationPayload* pNotify) const;

    /// Print the appropriate message for the breakpoint type using AGENT_OP
    void PrintHitMessage() const;

    /// This function calls the DBE for the first time, otherwise it remembers the GDB ID
    /// \param[in] dbeContextHandle The DBE context handle
    /// \param[in] GdbBkptId        The GDB ID for this breakpoint
    /// \return HSAIL agent status
    HsailAgentStatus CreateBreakpointDBE(const HwDbgContextHandle dbeContextHandle,
                                         const GdbBkptId          gdbId = g_UNKOWN_GDB_BKPT_ID);

    /// Delete breakpoint - for kernel source
    /// This function deletes the breakpoint in DBE only if no more GDB IDs are left
    ///
    /// \param[in] dbeContextHandle The DBE context handle
    /// \param[in] gdbId            The GDB ID that we want to delete for this breakpoint
    /// \return HSAIL agent status
    HsailAgentStatus DeleteBreakpointDBE(const HwDbgContextHandle dbeContextHandle,
                                         const GdbBkptId          gdbId = g_UNKOWN_GDB_BKPT_ID);

    /// Delete breakpoint - for kernel name
    /// \param[in] The GDB ID that we want to delete for this breakpoint
    /// \return HSAIL agent status
    HsailAgentStatus DeleteBreakpointKernelName(const GdbBkptId gdbID);

private:

    /// Disable copy constructor
    AgentBreakpoint(const AgentBreakpoint&);

    /// Disable assignment operator
    AgentBreakpoint& operator=(const AgentBreakpoint&);

    /// The BP handle for the DBE
    HwDbgCodeBreakpointHandle m_handle;
};
}
#endif // AGENT_BREAKPOINT_H_
