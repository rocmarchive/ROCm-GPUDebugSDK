//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief  An agent implementation for hsa to inject debugger calls into
///         the application process.
//==============================================================================
// Headers for signals
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

// HSA headers
#include <hsa_api_trace.h>
#include <hsa_ext_amd.h>
#include <amd_hsa_tools_interfaces.h>

// Module in Common/Src/DynamicLibraryModule
#include "HSADebuggerRTModule.h"

// HSA Debug Agent headers and parameters for shmem and fifo
#include "AgentContext.h"
#include "AgentConfiguration.h"
#include "AgentISABuffer.h"
#include "AgentLogging.h"
#include "AgentNotifyGdb.h"
#include "AgentUtils.h"
#include "CommunicationControl.h"
#include "CommandLoop.h"
#include "HSADebugAgent.h"
#include "HSAIntercept.h"

// HSA Callbacks
#include "PrePostDispatchCallback.h"


// A loader for the HSA runtime tools library
// This is a class similar to the loader in the DBE
class HSADebuggerRTLoader
{

public:
    HSADebuggerRTLoader():
        m_pDebuggerRTModule(nullptr)
    {
        AGENT_LOG("HSADebuggerRTLoader: Allocate runtime tools library loader");
    }

    ~HSADebuggerRTLoader()
    {
        AGENT_LOG("HSADebuggerRTLoader: Free the runtime tools library loader");
        delete m_pDebuggerRTModule;
        m_pDebuggerRTModule = nullptr;
    }

    /// Gets the HSA Tools Runtime Module, do the loading only once
    /// \return the HSA Tools Runtime Module
    HSADebuggerRTModule* CreateHSADebuggerRTModule()
    {
        if (nullptr == m_pDebuggerRTModule)
        {
            m_pDebuggerRTModule = new(std::nothrow) HSADebuggerRTModule();

            if (nullptr == m_pDebuggerRTModule || !m_pDebuggerRTModule->IsModuleLoaded())
            {
                AGENT_ERROR("HSADebuggerRTLoader: Unable to load runtime tools library");
            }
        }

        return m_pDebuggerRTModule;
    }

private:
    /// Module for the HSA Debugger Runtime
    HSADebuggerRTModule* m_pDebuggerRTModule;

};


static HSADebuggerRTLoader* psDebuggerRTLoader = nullptr;

// The AgentContext will be a global pointer
// This is needed since "OnUnload" does not get a argument
// We will delete the AgentContext object on unload
static HwDbgAgent::AgentContext* psAgentContext = nullptr;

static HwDbgAgent::AgentConfiguration* psActiveAgentConfig = nullptr;

// RT API functions
static CoreApiTable* gs_pCoreApiTable = nullptr;

// global flag to ensure initialization is done once
static bool gs_bInit = false;

static void InitAgentContext();

// This signal handler is needed since we pass SIGUSR1 to the inferior
// for debugging multithreaded programs
void tempHandleSIGUSR1(int signal)
{
    if (signal != SIGUSR1)
    {
        AGENT_ERROR("A spurious signal detected in initialization");
        AGENT_ERROR("We don't know what to do");
    }

    return;
}


// We initialize the AgentContext object by calling this function
// on the library load and then pass it to the predispatch callback
static void CreateHsaAgentContext()
{
    if (psAgentContext != nullptr)
    {
        AGENT_ERROR("Cannot reinitialize the agent context");
        AgentFatalExit();
    }

    psAgentContext = new(std::nothrow) HwDbgAgent::AgentContext;

    if (psAgentContext == nullptr)
    {
        AGENT_ERROR("Could not create a AgentContext for debug");
        AgentFatalExit();
    }

}

static void InitAgentConfiguration()
{
    if (psActiveAgentConfig == nullptr)
    {
        psActiveAgentConfig = new (std::nothrow) HwDbgAgent::AgentConfiguration;
    }
    else
    {
        AGENT_LOG("Agent has already been configured, skipping");
    }
    if (psActiveAgentConfig == nullptr)
    {
        AGENT_ERROR("Could not configure agent");
    }
}

static void ClearAgentConfiguration()
{
    if (psActiveAgentConfig != nullptr)
    {
        delete psActiveAgentConfig;
        psActiveAgentConfig = nullptr;
    }
    else
    {
        AGENT_ERROR("Could not delete AgentConfiguration");
    }
}

// Some of device info is not provided by the Runtime currently.
// Disable dumping this data until this is fixed in the Runtime.
#define  FULL_DEVICE_INFO   0

/// Callback function for "hsa_iterate_agents" called by SetDeviceInfo().
static hsa_status_t QueryDeviceCallback(hsa_agent_t agent, void* pData)
{
    int  status = HSA_STATUS_SUCCESS;
    if (gs_pCoreApiTable == nullptr)
    {
        AGENT_ERROR("API table is null in query device callback");
        return HSA_STATUS_ERROR;
    }

    RocmDeviceDesc  deviceDesc;
    memset(&deviceDesc, 0, sizeof(deviceDesc));
    char  nameBuf[2 * AGENT_MAX_DEVICE_NAME_LEN];
    memset(nameBuf, 0, 2 * AGENT_MAX_DEVICE_NAME_LEN);

    // Find out the device type and skip it if it's a CPU.
    hsa_device_type_t  deviceType;
    status = gs_pCoreApiTable->hsa_agent_get_info_fn(agent, HSA_AGENT_INFO_DEVICE, &deviceType);
    if (status == HSA_STATUS_SUCCESS && deviceType == HSA_DEVICE_TYPE_CPU)
    {
        return HSA_STATUS_SUCCESS;
    }

    status |= gs_pCoreApiTable->hsa_agent_get_info_fn(agent, HSA_AGENT_INFO_VENDOR_NAME, nameBuf);
    // Insert a space between the vendor and product names.
    size_t vendorNameLen = strnlen(nameBuf, AGENT_MAX_DEVICE_NAME_LEN);
    strncpy(nameBuf + vendorNameLen, " ", 1);
    status |= gs_pCoreApiTable->hsa_agent_get_info_fn(agent, HSA_AGENT_INFO_NAME, nameBuf + vendorNameLen + 1);
    memcpy(deviceDesc.m_deviceName, nameBuf, AGENT_MAX_DEVICE_NAME_LEN);

    status |= gs_pCoreApiTable->hsa_agent_get_info_fn(agent,
                                                    static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_CHIP_ID),
                                                    &deviceDesc.m_chipID);
    status |= gs_pCoreApiTable->hsa_agent_get_info_fn(agent,
                                                    static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_COMPUTE_UNIT_COUNT),
                                                    &deviceDesc.m_numCUs);
#if FULL_DEVICE_INFO
    status |= gs_pCoreApiTable->hsa_agent_get_info_fn(agent,
                                                    static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_NUM_SHADER_ENGINES),
                                                    &deviceDesc.m_numSEs);
    status |= gs_pCoreApiTable->hsa_agent_get_info_fn(agent,
                                                    static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_NUM_SIMDS_PER_CU),
                                                    &deviceDesc.m_numSIMDsPerCU);
#endif
    status |= gs_pCoreApiTable->hsa_agent_get_info_fn(agent,
                                                    static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_MAX_WAVES_PER_CU),
                                                    &deviceDesc.m_wavesPerCU);
    status |= gs_pCoreApiTable->hsa_agent_get_info_fn(agent,
                                                    static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_MAX_CLOCK_FREQUENCY),
                                                    &deviceDesc.m_maxEngineFreq);
    status |= gs_pCoreApiTable->hsa_agent_get_info_fn(agent,
                                                    static_cast<hsa_agent_info_t>(HSA_AMD_AGENT_INFO_MEMORY_MAX_FREQUENCY),
                                                    &deviceDesc.m_maxMemoryFreq);
    if (status != HSA_STATUS_SUCCESS)
    {
        AGENT_WARNING("Failed to get some of the device info");
    }

    psAgentContext->AddDeviceInfo(agent.handle, deviceDesc);

    return HSA_STATUS_SUCCESS;
}

/// Find out the list of available devices in the system and pass it to the Context.
static bool SetDeviceInfo()
{
    hsa_status_t  status = HSA_STATUS_SUCCESS;

    if (gs_pCoreApiTable == nullptr)
    {
        AGENT_WARNING("Old Runtime version; not sending device info to GDB.");
    }
    else
    {
        // Call the "hsa_iterate_agents" RT API function to get the information about available agents (devices).
        // The RT will call "QueryDeviceCallback" function for each agent.
        status = gs_pCoreApiTable->hsa_iterate_agents_fn(QueryDeviceCallback, nullptr);
        if (status != HSA_STATUS_SUCCESS)
        {
            AGENT_ERROR("Failed querying the device information.");
            return false;
        }
    }

    return true;
}

static void InitHsaAgent()
{
    if (!gs_bInit)
    {

        AGENT_LOG("===== HSADebugAgent activated =====");
        CreateCommunicationFifos();
        HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

        // Send the SIGALRM
        AgentNotifyGDB();

        signal(SIGUSR1, tempHandleSIGUSR1);

        AgentTriggerGDBEventLoop();

        status = InitFifoWriteEnd();

        if (status  != HSAIL_AGENT_STATUS_SUCCESS)
        {
            AGENT_ERROR("Could not initialize the fifo write end");
        }

        // This is needed to push the event loop along in gdb so we go to linux_nat_wait
        AgentTriggerGDBEventLoop();

        // SIGALRM count is now 2 in GDB
        AgentNotifyGDB();

        status = InitFifoReadEnd();

        if (status  != HSAIL_AGENT_STATUS_SUCCESS)
        {
            AGENT_ERROR("Could not initialize the fifo read end");
        }

        AgentTriggerGDBEventLoop();

        AGENT_LOG("===== Fifos initialized===== ");

        // Now that GDB has started, allocate the Agent Context object
        InitAgentContext();
        gs_bInit = true;

    }
    else
    {
        AGENT_LOG("HSA Agent is already loaded");
    }
}



static void InitAgentContext()
{
    if (!gs_bInit)
    {
        AGENT_LOG("===== Init AgentContext =====");

        // The agent state object is global here
        CreateHsaAgentContext();

        // We can change agentcontext's state once we have successfully set it as the
        // userarg for the predispatch function
        HsailAgentStatus agentStatus = psAgentContext->Initialize();

        if (agentStatus != HSAIL_AGENT_STATUS_SUCCESS)
        {
            AGENT_ERROR("g_pAgentContext returned an error.");
            return;
        }

        // Set the device info
        if (SetDeviceInfo() == false)
        {
            AGENT_ERROR("Could not get devices info");
        }
    }
}

// This function is called from both, the intercepted hsa_shut_down
// and the OnUnload function, in the case the application does not call
// hsa_shut_down for some reason we hope that atleast UnLoad is called
// (doesnt seem to be the case for now though)
void ShutDownHsaAgentContext(const bool skipDbeShutDown)
{
    HsailAgentStatus status = psAgentContext->ShutDown(skipDbeShutDown);

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("ShutDownHsaAgentContext: Could not close the AgentContext");
    }

}
// global flag to ensure cleanup is done once
bool g_bCleanUp = false;


// We clear up the AgentContext by deleting the object when the
// agent is unloaded
static void DeleteHsaAgentContext()
{
    if (!g_bCleanUp)
    {
        if (psAgentContext != nullptr)
        {
            delete psAgentContext;
            psAgentContext = nullptr;
        }
        else
        {
            AGENT_ERROR("Could not delete AgentContext");
        }

        g_bCleanUp = true;
        gs_bInit = false;
    }
}

static void CloseCommunicationFifo()
{
    if (!g_bCleanUp)
    {
        // The unlinking  (deleted from the file-system) of the fifo's is
        // done in the gdb end since we know that linux_nat_close
        // will be called later than the CleanUpHsaAgent() functions.

        AGENT_LOG("CloseCommunicationFifo: HSADebugAgent Cleanup");
        // Close the Agent <== GDB fifo
        int readFifoDescriptor = GetFifoReadEnd();
        close(readFifoDescriptor);

        // Close the Agent ==> GDB fifo
        int writeFifoDescriptor = GetFifoWriteEnd();
        close(writeFifoDescriptor);
    }
}

// Check the version based on the provided by HSA runtime's OnLoad function.
// The logic is based on code in the HSA profiler (HSAPMCAgent.cpp).
// Return success if the versions match.
static HsailAgentStatus AgentCheckVersion(uint64_t runtimeVersion, uint64_t failedToolCount,
                                          const char* const* pFailedToolNames)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    static const std::string HSA_RUNTIME_TOOLS_LIB("libhsa-runtime-tools64.so.1");

    if (failedToolCount > 0 && runtimeVersion > 0)
    {
        if (pFailedToolNames != nullptr)
        {
            for (uint64_t i = 0; i < failedToolCount; i++)
            {
                if (pFailedToolNames[i] != nullptr)
                {
                    std::string failedToolName = std::string(pFailedToolNames[i]);

                    if (std::string::npos != failedToolName.find_last_of(HSA_RUNTIME_TOOLS_LIB))
                    {
                        AGENT_OP("rocm-gdb not enabled. Version mismatch between ROCm runtime and "
                                 << HSA_RUNTIME_TOOLS_LIB);
                        AGENT_ERROR("Debug agent not enabled. Version mismatch between ROCm runtime and "
                                    << HSA_RUNTIME_TOOLS_LIB);
                    }
                }
                else
                {
                    AGENT_ERROR("Debug agent not enabled," << HSA_RUNTIME_TOOLS_LIB
                                << "version could not be verified");
                    AGENT_ERROR("AgentCheckVersion: pFailedToolNames[" << i << "] is nullptr");
                }
            }
            return status;
        }
        else
        {
            AGENT_ERROR("AgentCheckVersion: Could not verify version successfully");
        }
    }
    else
    {
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }

    return status;
}

HwDbgAgent::AgentConfiguration* GetActiveAgentConfig()
{
    if (psActiveAgentConfig == NULL)
    {
        AGENT_LOG("Returning a NULL AgentConfiguration");
    }

    return psActiveAgentConfig;
}

HsailAgentStatus InitDispatchCallbacks(hsa_queue_t* queue)
{
    AGENT_LOG("Setup the HSADebugAgent callbacks");
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    if (queue == nullptr)
    {
        AGENT_ERROR("Could not set the dispatch callbacks, the queue is nullptr");
        return status;
    }

    hsa_status_t hsaStatus ;
    hsaStatus = psDebuggerRTLoader->CreateHSADebuggerRTModule()
                ->ext_tools_set_callback_functions(queue,
                                                   HwDbgAgent::PreDispatchCallback,
                                                   HwDbgAgent::PostDispatchCallback);

    if (hsaStatus != HSA_STATUS_SUCCESS)
    {
        AGENT_ERROR(GetHsaStatusString(hsaStatus).c_str());
        AGENT_ERROR("hsa_ext_tools_set_callback_functions returns an error.");
        return status;
    }

    // Assign the agent state to the predispatchcallback's arguments
    hsaStatus = psDebuggerRTLoader->CreateHSADebuggerRTModule()
                ->ext_tools_set_callback_arguments(queue,
                                                   reinterpret_cast<void*>(psAgentContext),
                                                   nullptr);

    if (hsaStatus != HSA_STATUS_SUCCESS)
    {
        AGENT_ERROR(GetHsaStatusString(hsaStatus).c_str());
        AGENT_ERROR("hsa_ext_tools_set_callback_arguments returns an error.");
        return status;
    }

    status = HSAIL_AGENT_STATUS_SUCCESS;
    return status;
}

extern "C" bool OnLoad(void* pTable,
                       uint64_t runtimeVersion, uint64_t failedToolCount,
                       const char* const* pFailedToolNames)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    InitAgentConfiguration();

    status = AgentInitLogger();

    if (status  != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not initialize Logging");
        return false;
    }

    // Start the DBE, this will initialize the DBE's internal Tools RT loaders
    HwDbgStatus dbeStatus = HwDbgInit(reinterpret_cast<void*>(pTable));

    if (dbeStatus  != HWDBG_STATUS_SUCCESS)
    {
        AGENT_ERROR("HwDbgInit failed: DBE Status" << GetDBEStatusString(dbeStatus));
        return false;

    }

    AGENT_LOG("===== Load GDB Tools Agent=====");

    status = AgentCheckVersion(runtimeVersion, failedToolCount, pFailedToolNames);

    if (status  != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Version mismatch");
        return false;
    }


    if (0 == runtimeVersion)
    {
        AGENT_ERROR("Unsupported runtime version");
        status = HSAIL_AGENT_STATUS_FAILURE;
    }
    else
    {
        gs_pCoreApiTable = (reinterpret_cast<HsaApiTable*>(pTable))->core_;
        status = InitHsaCoreAgentIntercept(reinterpret_cast<HsaApiTable*>(pTable));
    }

    if (status  != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not initialize dispatch tables");
        return false;
    }

    psDebuggerRTLoader = new(std::nothrow) HSADebuggerRTLoader;

    if (psDebuggerRTLoader == nullptr)
    {
        AGENT_ERROR("Could not initialize the Agent's HSA RT module");
        return false;
    }

    // Initialize the communication with GDB
    InitHsaAgent();

    AGENT_LOG("===== Finished Loading GDB Tools Agent=====");

    return true;
}


extern "C" void OnUnload()
{
    AGENT_LOG("===== Unload GDB Tools Agent=====");
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    status = HwDbgAgent::WaitForDebugThreadCompletion();

    if (status  != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("OnUnload:Error waiting for the debug thread to complete");
    }

    // We skip the DBE shutdown when we try to shutdown the AgentContext
    // since the HSA tools RT may already have been unloaded.
    ShutDownHsaAgentContext(true);

    CloseCommunicationFifo();

    // The agentcontext object is global here since the unload function
    // does not have an UserArg parameter
    // We need to be sure that all debug is over before we call this
    DeleteHsaAgentContext();

    if (psDebuggerRTLoader == nullptr)
    {
        AGENT_ERROR("OnUnload:Could not delete the debugger RT loader");
    }
    else
    {
        delete psDebuggerRTLoader ;
    }

    status = AgentCloseLogger();

    if (status  != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("OnUnload:Could not close Logging");
    }

    ClearAgentConfiguration();
}
