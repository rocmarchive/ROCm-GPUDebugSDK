//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Implementation of the Agent context
//==============================================================================
#ifndef AGENT_CONTEXT_H_
#define AGENT_CONTEXT_H_

#include <string>
#include <vector>

// HSA headers
#include <hsa.h>

// DBE Headers
#include "AMDGPUDebug.h"

//Agent headers
#include "CommunicationControl.h"

namespace HwDbgAgent
{
class AgentBinary;
class AgentBreakpointManager;
class AgentFocusWaveControl;
class AgentWavePrinter;

typedef enum
{
    HSAIL_PARENT_STATUS_UNKNOWN,        /// Parent status is unknown
    HSAIL_PARENT_STATUS_GOOD,           /// The getppid() function OP matches the saved ppid
    HSAIL_PARENT_STATUS_TERMINATED,     /// getppid() function does not match the saved ppid
    HSAIL_PARENT_STATUS_CHECK_COUNT_MAX /// This check happened too many times
} HsailParentStatus;

/// The AgentContext includes functionality to start and stop debug.
/// The AgentContext is initialized when the agent is loaded and passed as the UserArg field
/// to the predispatch callback function.
/// It is deleted when the agent is unloaded.
class AgentContext
{
private:

    /// This enum is private since we don't want others to change the AgentContext state
    typedef enum _HsailAgentState
    {
        HSAIL_AGENT_STATE_UNKNOWN,          /// We havent set it yet
        HSAIL_AGENT_STATE_OPEN,             /// The agent is open, all initialization steps are complete
        HSAIL_AGENT_STATE_BEGIN_DEBUGGING,  /// HwDBg has started
        HSAIL_AGENT_STATE_END_DEBUGGING,    /// HwDbg has ended (We are not bothering with adding post breakpoint here)
        HSAIL_AGENT_STATE_CLOSED            /// The agent has been closed by GDB

    } HsailAgentState;

    /// All available GPU devices in the system.
    struct
    {
        std::vector<uint64_t>        handles;
        std::vector<RocmDeviceDesc>  deviceDescs;
    } m_devices;

    std::vector<AgentBinary*> m_pKernelBinaries;

    /// Enum that describes the state of the agent
    HsailAgentState m_AgentState ;

    /// Input passed to the DBE
    HwDbgState m_HwDebugState;

    /// This parameter is passed to the DBE with every call
    HwDbgContextHandle m_DebugContextHandle;

    /// The last DBE event type
    HwDbgEventType m_LastEventType;

    /// The parent process ID
    int m_ParentPID;

    /// Key for the code object buffer's shared memory
    int m_codeObjBufferShmKey;

    /// Max size for the code object buffer's shared memory
    size_t m_codeObjBufferMaxSize;

    /// Key for the code object buffer's shared memory
    int m_loadMapBufferShmKey;

    /// Max size for the code object buffer's shared memory
    size_t m_loadMapBufferMaxSize;

    /// Disable copy constructor
    AgentContext(const AgentContext&);

    /// Disable assignment operator
    AgentContext& operator=(const AgentContext&);

    /// Private function to begin debugging
    HsailAgentStatus BeginDebugging();

    /// Release the latest kernel binary we have.
    /// Called when we do EndDebug with HWDBG_BEHAVIOR_NONE or when we register any new binary
    HsailAgentStatus ReleaseKernelBinary();

    /// These functions are called only once irrespective of how many ever binaries
    /// are encountered by the application. Thats why this func is part of the context
    /// rather than the AgentBinary class.
    /// The key is not passed since its global
    /// Private function to initialize the shared memory buffer for the binary
    HsailAgentStatus AllocateBinaryandLoadMapSharedMem();

    /// Private function to free the shared memory buffer for the binary
    HsailAgentStatus FreeBinaryandLoadMapSharedMem();

public:
    /// A bit to track that we have received the continue command from the host
    bool m_ReadyToContinue;

    /// The active dispatch dimensions populated from the Aqlpacket when begin debug
    HwDbgDim3 m_workGroupSize;

    /// The active dispatch dimensions populated from the Aqlpacket when begin debug
    HwDbgDim3 m_gridSize;

    /// The breakpoint manager we use for this context
    AgentBreakpointManager* m_pBPManager;

    /// The wave printer we use for this context
    AgentWavePrinter* m_pWavePrinter;

    /// The focus wave control we use for this context
    AgentFocusWaveControl* m_pFocusWaveControl;

    AgentContext();

    /// Destructor that shuts down the AgentContext if not already shut down.
    ~AgentContext();

    /// This function is called when the agent is loaded and the handshake
    /// with GDB is complete
    /// It only captures that the AgentContext object in now in a HSAIL_AGENT_STATE_OPEN
    /// state and debugging has not yet started.
    HsailAgentStatus Initialize();

    /// Shutdown API, will be called in destructor if not called explicitly
    /// If true, we skip the DBE shutdown call. That way we can call this function
    /// in the Unload too
    HsailAgentStatus ShutDown(const bool skipDbeShutDown);

    /// Begin debugging
    /// Assumes only one session active at a time
    /// This function takes individual HSA specific parameters and then populates
    /// the HwDbgState
    HsailAgentStatus BeginDebugging(const hsa_agent_t                   agent,
                                    const hsa_queue_t*                  pQueue,
                                          hsa_kernel_dispatch_packet_t* pAqlPacket,
                                          uint32_t                      behaviorFlags);

    /// Resume debugging, does all the state checks internally
    HsailAgentStatus ContinueDebugging();

    /// Force complete dispatch
    HsailAgentStatus ForceCompleteDispatch();

    /// End debugging, does not close the agent
    /// Force the cleanup in the DBE if true, does not force the cleanup by default
    HsailAgentStatus EndDebugging(const bool& forceCleanUp = false);

    /// Kill all waves
    HsailAgentStatus KillDispatch();

    /// Save the code object in the AgentContext
    HsailAgentStatus AddKernelBinaryToContext(AgentBinary* pAgentBinary);

    /// The wrapper around the DBE's function
    HsailAgentStatus WaitForEvent(HwDbgEventType* pEventTypeOut);

    /// Accessor method to return the active context, needed for things like Breakpoints
    const HwDbgContextHandle GetActiveHwDebugContext() const;

    /// Just a logging function
    const std::string GetAgentStateString() const;

    const hsa_kernel_dispatch_packet_t* GetDispatchedAQLPacket() const;

    /// Accessor method to get the BP manager for this context
    AgentBreakpointManager* GetBpManager() const;

    /// Accessor method to return the wave printer for this context
    AgentWavePrinter* GetWavePrinter() const;

    /// Accessor method to return the focus wave controller for this context
    AgentFocusWaveControl* GetFocusWaveControl() const;

    /// Return true if HwDebug has started
    bool HasHwDebugStarted() const;

    /// Just a logging function
    HsailAgentStatus PrintDBEVersion() const;

    /// Compare parent PID saved at object creation with present parent PID
    bool CompareParentPID() const;

    /// Add a device info to the list of available devices
    /// \param[in] handle    New device handle to be added to the list of handles.
    /// \param[in] device    New device descriptor to be added to the list of descriptors.
    void AddDeviceInfo(uint64_t handle, RocmDeviceDesc& device);

    /// Set active device
    /// \param[in] handle    The devide handle corresponding to the currently active device.
    void SetActiveDevice(uint64_t handle);
};

} // End Namespace HwDbgAgent

#endif // AGENT_CONTEXT_H_
