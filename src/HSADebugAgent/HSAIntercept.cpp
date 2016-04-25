//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Intercepting HSA Signal management calls and track validity of the signal
//==============================================================================

#include <cstdlib>
#include <cstring>
#include <string>

#include <hsa.h>
#include <hsa_api_trace.h>
#include <hsa_ext_amd.h>
#include <hsa_ext_finalize.h>

#include <amd_hsa_tools_interfaces.h>

#include "AgentISABuffer.h"
#include "AgentLogging.h"
#include "AgentUtils.h"
#include "CommunicationControl.h"
#include "HSADebugAgent.h"
#include "HSAIntercept.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/// const representing the min required queue size for SoftCP mode
/// This value is copied from HSAPMCInterceptionHelpers.h in the profiler
static const uint32_t MIN_QUEUE_SIZE_FOR_SOFTCP = 128;

// The HSA Runtime's versions of HSA API functions
ApiTable g_OrigCoreApiTable;

// The HSA Tools Runtime versions of the tools RT functions
ExtTable g_OrigHSAExtTable;

hsa_status_t
HsaDebugAgent_hsa_shut_down()
{
     AGENT_LOG("Interception: hsa_shut_down");

     ShutDownHsaAgentContext(false);

    hsa_status_t rtStatus;
    rtStatus = g_OrigCoreApiTable.hsa_shut_down_fn();

    // Note:
    // The AGENT_LOG statements below this will not usually print since we
    // the HSA RT presently calls the UnLoad function in the hsa_shut_down
    // which in turn closes AGENT_LOG.
    // If that behavior changes, we should see the corresponding Interception: Exit entry

    if (rtStatus != HSA_STATUS_SUCCESS)
     {
         AGENT_ERROR("Interception: Error in hsa_shut_down " << GetHsaStatusString(rtStatus));
         return rtStatus;
     }

    AGENT_LOG("Interception: Exit hsa_shut_down");

    return rtStatus;
}


hsa_status_t
HsaDebugAgent_hsa_queue_create(hsa_agent_t agent,
                               uint32_t size,
                               hsa_queue_type_t type,
                               void (*callback)(hsa_status_t status, hsa_queue_t* source,
                                                void* data),
                               void* data,
                               uint32_t private_segment_size,
                               uint32_t group_segment_size,
                               hsa_queue_t** queue)
{
    AGENT_LOG("Interception: hsa_queue_create");

    hsa_status_t rtStatus;

    // SoftCP mode requires a queue to be able to handle at least 128 packets
    if (MIN_QUEUE_SIZE_FOR_SOFTCP > size)
    {

        AGENT_OP("rocm-gdb is overriding the queue size passed to hsa_queue_create."
                 << "Queues must have a size of at least "
                 << MIN_QUEUE_SIZE_FOR_SOFTCP << " for debug.");

        AGENT_LOG("rocm-gdb is overriding the queue size passed to hsa_queue_create."
                  << "Queues must have a size of at least "
                  << MIN_QUEUE_SIZE_FOR_SOFTCP << " for debug.");

        size = MIN_QUEUE_SIZE_FOR_SOFTCP;
    }

    rtStatus = g_OrigCoreApiTable.hsa_queue_create_fn(agent,
                                                      size,
                                                      type,
                                                      callback,
                                                      data,
                                                      private_segment_size,
                                                      group_segment_size,
                                                      queue);

    if (rtStatus != HSA_STATUS_SUCCESS || *queue == nullptr)
    {
        AGENT_ERROR("Interception: Could not create a valid Queue, debugging will not work" <<
                    GetHsaStatusString(rtStatus));
        return rtStatus;
    }

    HsailAgentStatus status = InitDispatchCallbacks(*queue);

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Interception: Could not configure queue for debug");
        // I am inclined to make rtStatus an error, we plainly want to debug but cant do so
    }

    AGENT_LOG("Interception: Exit hsa_queue_create");

    return rtStatus;

}


hsa_status_t
HsaDebugAgent_hsa_ext_program_finalize(hsa_ext_program_t            program,
                                       hsa_isa_t                    isa,
                                       int32_t                      call_convention,
                                       hsa_ext_control_directives_t control_directives,
                                       const char*                        options,
                                       hsa_code_object_type_t       code_object_type,
                                       hsa_code_object_t*           code_object)
{
    AGENT_LOG("Interception: hsa_ext_program_finalize");


    // We can check for debug flags, which should be added by the rocm-gdb wrapper script
    // via a env variable
    //
    // We check for errors that can occur if someone managed to run rocm-gdb outside of the script
    // or if the env variable mechanism to append flags is broken

    static const std::string s_KNOWN_FINALIZER_FLAGS = "-g -O0 -amd-reserved-num-vgprs=4 ";

    // Read and save the input flags from the function arguments
    std::string ipFinalizerOptions("");
    if (options != nullptr)
    {
        ipFinalizerOptions.assign(options);
    }
    AGENT_LOG("Interception: Options for finalizer: "<< "\"" << ipFinalizerOptions << "\"" );

    // We first check if the env variable provides the debug flags,
    // if not we check if the input arguments provide the debug flags
    char* pfinalizerEnvVar;
    std::string debugFlagsFromEnvvar("");
    pfinalizerEnvVar = std::getenv("PROGRAM_FINALIZE_OPTIONS_APPEND");
    if (pfinalizerEnvVar != nullptr)
    {
        debugFlagsFromEnvvar.assign(pfinalizerEnvVar);
        AGENT_LOG("PROGRAM_FINALIZE_OPTIONS_APPEND: " << debugFlagsFromEnvvar);
    }

    // If we couldn't find the debug flags in the env variable, we can check if t
    // they are in the input arguments.
    // If both are not available, we cant debug
    if (debugFlagsFromEnvvar.find(s_KNOWN_FINALIZER_FLAGS) == std::string::npos )
    {
        AGENT_LOG("Interception: Finalizer input arguments:" << ipFinalizerOptions);

        if (ipFinalizerOptions.find(s_KNOWN_FINALIZER_FLAGS) == std::string::npos)
        {
            AGENT_ERROR("This HSA program has not been finalized with debug options.");
            AGENT_ERROR("Please finalize the program with " <<
                        "\""<< s_KNOWN_FINALIZER_FLAGS <<"\"");
        }
    }

     hsa_status_t status = g_OrigHSAExtTable.hsa_ext_program_finalize_fn(program,
                                                                         isa,
                                                                         call_convention,
                                                                         control_directives,
                                                                         ipFinalizerOptions.c_str(),
                                                                         code_object_type,
                                                                         code_object);

    if (status != HSA_STATUS_SUCCESS)
    {
        AGENT_ERROR("Interception: HSA Runtime could not finalize the kernel " <<
                    GetHsaStatusString(status));
    }

    // Presently when we use amdhsacod, we just use the ISA obtained from the code object
    // We dont need to use the ISA objects cached by the Manager

    // \todo fix this so that we dont delete the temp ISA file if the user wants to dump
    // all the GPU ISA
    // Save the ISA, delete the finalizer generated file
    //HsailAgentStatus agentStatus = GetActiveISABufferManager()->AppendISABuffer();
    //
    //if (agentStatus != HSAIL_AGENT_STATUS_SUCCESS)
    //{
    //    AGENT_LOG("hsa_ext_program_finalize: The ISA could not be saved");
    //}

    AGENT_LOG("Interception: Exit hsa_ext_program_finalize");

    return status;

}

hsa_status_t HsaDebugAgent_hsa_executable_load_code_object(
    hsa_executable_t executable,
    hsa_agent_t agent,
    hsa_code_object_t code_object,
    const char *options)
{
    hsa_status_t rtStatus = HSA_STATUS_ERROR;
    AGENT_LOG("Interception: hsa_executable_load_code_object");
    AGENT_LOG("IP options " << options);

    rtStatus = g_OrigCoreApiTable.hsa_executable_load_code_object_fn(executable,
                                                                     agent,
                                                                     code_object,
                                                                     options);

    // Presently when we use amdhsacod, we just use the ISA obtained from the code object
    // We dont need to use the ISA objects cached by the Manager

    // Save the ISA, delete the finalizer generated file
    //HsailAgentStatus agentStatus = GetActiveISABufferManager()->AppendISABuffer();
    //
    //if (agentStatus != HSAIL_AGENT_STATUS_SUCCESS)
    //{
    //    AGENT_LOG("hsa_executable_load_code_object: The ISA could not be saved");
    //}


    AGENT_LOG("Interception: Exit hsa_executable_load_code_object");

    return rtStatus;

}

static void UpdateHSAFunctionTable(ApiTable* pCoreTable)
{
    if (pCoreTable == nullptr)
    {
        return;
    }

    AGENT_LOG("Interception: Replace functions with HSADebugAgent versions");

    pCoreTable->hsa_queue_create_fn = HsaDebugAgent_hsa_queue_create;
    pCoreTable->hsa_shut_down_fn    = HsaDebugAgent_hsa_shut_down;
    pCoreTable->hsa_executable_load_code_object_fn = HsaDebugAgent_hsa_executable_load_code_object;


    pCoreTable->std_exts_->hsa_ext_program_finalize_fn = HsaDebugAgent_hsa_ext_program_finalize;
}


// This function will be extended with the kernel compilation interception too
HsailAgentStatus InitHsaCoreAgentIntercept(ApiTable* pTable)
{

    AGENT_LOG("InitHsaCoreAgentIntercept: Read HSA API Table");

    if (pTable == nullptr)
    {
        AGENT_ERROR("InitHsaCoreAgentIntercept: HSA Runtime provided a nullptr API Table");
        return HSAIL_AGENT_STATUS_FAILURE;
    }

    // This saves the original pointers
    memcpy(static_cast<void*>(&g_OrigCoreApiTable),
           static_cast<const void*>(pTable),
           sizeof(ApiTable));

    memcpy(static_cast<void*>(&g_OrigHSAExtTable),
           static_cast<void*>(pTable->std_exts_),
           sizeof(ExtTable));

    // We override the table that we get from the runtime
    UpdateHSAFunctionTable(pTable);

    AGENT_LOG("InitHsaCoreAgentIntercept: Finished updating HSA API Table");
    return HSAIL_AGENT_STATUS_SUCCESS;
}

#ifdef __cplusplus
}
#endif  // __cplusplus
