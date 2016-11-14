//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Header for the breakpoint manager class
//==============================================================================
#ifndef AGENT_BREAKPOINT_MANANGER_H_
#define AGENT_BREAKPOINT_MANANGER_H_

// Relevant STL
#include <vector>
#include <sstream>

#include "hsa.h"
#include "AMDGPUDebug.h"

#include "AgentBreakpoint.h"
#include "CommunicationControl.h"

namespace HwDbgAgent
{

class AgentBreakpoint;
class AgentFocusWaveControl;

/// A class that works with HSAIL packets and DBE context information
/// and maintains a vector of AgentBreakpoint
/// The original implementation of this class was two vectors (m_BreakpointHandleList) and
/// (m_BreakpointGdbIdList) which would stay in sync. The two Breakpoint handle and the GDB
/// ID are now part of the AgentBreakpoint class
/// This class will maintain the breakpoint and source line information for a single kernel
class AgentBreakpointManager
{
private:

    /// A vector of breakpoint pointers
    std::vector<AgentBreakpoint*> m_pBreakpoints;

    /// A vector of momentary breakpoint pointers
    std::vector<AgentBreakpoint*> m_pMomentaryBreakpoints;

    /// Name of the file where the hsail kernel source is saved
    std::string m_kernelSourceFilename;

    /// Allocate the shared mem for the momentary breakpoints
    HsailAgentStatus AllocateMomentaryBPBuffer() const;

    /// Free the shared mem for the momentary breakpoints
    HsailAgentStatus FreeMomentaryBPBuffer() const;

    /// Key for the momentary breakpoints shared memory location
    int m_momentaryBPShmKey;

    /// Max size for the momentary breakpoints shared memory location
    size_t m_momentaryBPShmMaxSize;

    /// Check for duplicate source and function breakpoints
    /// \return true if any duplicates present
    bool IsDuplicatesPresent(const HwDbgContextHandle  DbeContextHandle,
                             const HsailCommandPacket& ipPacket,
                             const HsailBkptType       ipType);

    /// Called internally when we need a new temp breakpoint
    GdbBkptId CreateNewTempBreakpointId();

    /// To clear up memory when we destroy the breakpoint manager.
    HsailAgentStatus ClearBreakpointVectors();

    /// Enable all the momentary breakpoints, done as part of the EnableAllPCBreakpoints
    /// \param[in] dbeHandle The active debug context's handle
    HsailAgentStatus EnableAllMomentaryBreakpoints(const HwDbgContextHandle dbeHandle);

    /// Function to query the breakpoint vector for a certain PC
    /// \return position of the breakpoint object - based on PC
    bool GetBreakpointFromPC(const HwDbgCodeAddress pc,
                                   int*             pBreakpointPosOut,
                                   bool*            pMomentaryBreakpointOut) const;

    /// Utility to check if the PC is a breakpoint anywhere
    /// \return true iff there's a PC breakpoint on the value indicated
    bool IsPCBreakpoint(const HwDbgCodeAddress pc) const;

    /// Utility to check if a PC already exists in the breakpoint vectors
    /// \param[in] m_pc                 An input PC
    /// \param[out duplicatePosition    The location of the breakpoint with the PC
    bool IsPCExists(const HwDbgCodeAddress inputPC, int& duplicatePosition) const;

    /// Utility to get a the breakpoint from a GDB ID
    /// \param[in] GdbBkptId            A GDB ID
    /// \param[out] pBreakpointPosOut   Position of the breakpoint object - based on GDBID
    bool GetBreakpointFromGDBId(const GdbBkptId ipId, int* pBreakpointPosOut) const;

    /// Utility function to print the wave info for the breakpoint we just hit
    void PrintWaveInfo(const HwDbgWavefrontInfo* pWaveInfo, const HwDbgDim3* pFocusWI = nullptr) const;

    /// Disable copy constructor
    AgentBreakpointManager(const AgentBreakpointManager&);

    /// Disable assignment operator
    AgentBreakpointManager& operator=(const AgentBreakpointManager&);

public:

    /// Construct a breakpoint manager, also allocate the shared memory needed for momentary breakpoints
    AgentBreakpointManager();

    /// Destructor
    ~AgentBreakpointManager();

    /// Take in the context and an input packet and create a breakpoint
    /// \param[in] dbeHandle    The active debug context's handle
    /// \param[in] ipPacket     The packet obtained from GDB
    /// \param[in] ipType       The breakpoint type requested
    HsailAgentStatus CreateBreakpoint(const HwDbgContextHandle            dbeHandle,
                                      const hsa_kernel_dispatch_packet_t* pAqlPacket,
                                      const HsailCommandPacket            ipPacket,
                                      const HsailBkptType                 ipType);

    /// Take in the context and an input packet and delete the breakpoint
    /// \param[in] dbeHandle The active debug context's handle
    /// \param[in] ipPacket     The packet obtained from GDB
    HsailAgentStatus DeleteBreakpoint(const HwDbgContextHandle dbeHandle,
                                      const HsailCommandPacket ipPacket);

    /// This function is added to check the need for a temporary breakpoint.
    /// Needed for a step in if the developer has not set a breakpoint already
    /// Based on the number of enabled breakpoints, developer can call the temp bkptp API
    /// "type" filters on the breakpoint type, unless it's set to unknown, in which case
    /// it will return all breakpoints in the state:
    int GetNumBreakpointsInState(const HsailBkptState ipState,
                                 const HsailBkptType  type = HSAIL_BREAKPOINT_TYPE_UNKNOWN) const;

    /// Count the number of momentary breakpoints
    int GetNumMomentaryBreakpointsInState(const HsailBkptState ipState,
                                          const HsailBkptType  type = HSAIL_BREAKPOINT_TYPE_PC_BP) const;

    /// Returns true iff there is a kernel name breakpoint set against the input parameter name
    bool CheckAgainstKernelNameBreakpoints(const std::string& kernelName, int* pBpPositionOut) const;

    /// Disable a breakpoint
    /// \param[in] dbeHandle The active debug context's handle
    /// \param[in] ipPacket  The packet obtained from GDB
    HsailAgentStatus DisablePCBreakpoint(const HwDbgContextHandle dbeHandle,
                                         const HsailCommandPacket ipPacket);

    /// Disable all breakpoints (source and momentary) by deleting them in the DBE
    /// \param[in] dbeHandle The active debug context's handle
    HsailAgentStatus DisableAllBreakpoints(HwDbgContextHandle dbeHandle);

    /// Enable all breakpoints, needed for when we are in the predispatch callback
    /// We need to call the DBE for each breakpoint to enable them
    /// \param[in] dbeHandle The active debug context's handle
    HsailAgentStatus EnableAllPCBreakpoints(const HwDbgContextHandle dbeHandle);

    /// Enable a breakpoint
    /// \param[in] dbeHandle The active debug context's handle
    HsailAgentStatus EnablePCBreakpoint(const HwDbgContextHandle dbeHandle,
                                        const HsailCommandPacket ipPacket);

    /// Create a momentary breakpoint
    /// \param[in] dbeHandle The active debug context's handle
    HsailAgentStatus CreateMomentaryBreakpoints(const HwDbgContextHandle dbeHandle,
                                                const HsailCommandPacket ipPacket);

    /// Clear all momentary breakpoints
    /// \param[in] dbeHandle The active debug context's handle
    HsailAgentStatus ClearMomentaryBreakpoints(const HwDbgContextHandle dbeHandle);

    /// Checks the eventtype and then checks all the breakpoint PCs against the active wave PCs
    /// \param[in] dbeEventType      The event returned by the DBE, used to check where this function is called
    /// \param[in] dbeContextHandle  The active debug context's handle
    /// \param[in] pFocusWaveControl Focus wave controller that will be used to change the focus if
    ///                              the printed wave is different from the current focus wave
    /// \param[out] pIsStopNeeded    Returns true if the debug thread needs to call the stop
    HsailAgentStatus PrintStoppedReason(const HwDbgEventType         dbeEventType,
                                        const HwDbgContextHandle     dbeContextHandle,
                                              AgentFocusWaveControl* pFocusWaveControl,
                                              bool*                  pIsStopNeeded);

    /// Update the hit count of each breakpoint based on what we get from GetActiveWaves
    /// EventType is passed just to check that the DBE is in the right state before calling
    /// \param[in] dbeEventType     The event returned by the DBE, used to check where this function is called
    /// \param[in] dbeContextHandle The active debug context's handle
    HsailAgentStatus UpdateBreakpointStatistics(const HwDbgEventType     dbeEventType,
                                                const HwDbgContextHandle dbeContextHandle);

    /// Update the breakpoint statistics for kernel function breakpoints
    HsailAgentStatus ReportFunctionBreakpoint(const std::string& kernelFunctionName);

};

} // End Namespace HwDbgAgent

#endif // AGENT_BREAKPOINT_MANANGER_H_
