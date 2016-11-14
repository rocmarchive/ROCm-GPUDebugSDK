//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Agent side Implementation of the Hsail-gdb configuration manager
//==============================================================================
#include <fstream>

#include "AgentConfiguration.h"
#include "AgentLogging.h"
#include "CommunicationParams.h"

namespace HwDbgAgent
{
/// Constructor
AgentConfiguration::AgentConfiguration():
    m_configFileName("hsail-gdb.cfg")
{
    HsailAgentStatus status  = ConfigureAgent();
    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not configure the agent");
    }
}


HsailAgentStatus AgentConfiguration::ConfigureAgent()
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    if (ReadDefaultConfiguration())
    {
        status = HSAIL_AGENT_STATUS_SUCCESS;
        return status;
    }
    else
    {
        return status;
    }

    // \todo
    // We now need to check if the file exists.
    // We then need to validate the parameters and then overwrite the member data
    return status;
}

HsailAgentStatus AgentConfiguration::GetConfigShmKey(const HsailDebugConfigParam requestedParam,
                                                          int&                  outKey) const
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    // If the entry exists
    if (m_configMap.find(requestedParam) != m_configMap.end())
    {
        if (m_configMap.at(requestedParam).paramType == requestedParam)
        {
            outKey = m_configMap.at(requestedParam).param.shmemParam.m_shmKey;
            status = HSAIL_AGENT_STATUS_SUCCESS;
        }
    }

    return status;
}

HsailAgentStatus AgentConfiguration::GetConfigShmSize(const HsailDebugConfigParam requestedParam,
                                                          size_t&               outSize)const
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    // If the entry exists
    if (m_configMap.find(requestedParam) != m_configMap.end())
    {
        if (m_configMap.at(requestedParam).paramType == requestedParam)
        {
            outSize = m_configMap.at(requestedParam).param.shmemParam.m_maxSize;
            status = HSAIL_AGENT_STATUS_SUCCESS;
        }
    }

    return status;
}

bool AgentConfiguration::ValidateFile() const
{
    bool retCode = false;
    AGENT_ERROR("Unimplemented function");
    // For each parameter, check that it is unique
    return retCode;
}

bool AgentConfiguration::ReadDefaultConfiguration()
{
    bool retCode = false;

    m_configMap[HSAIL_DEBUG_CONFIG_CODE_OBJ_SHM].paramType = HSAIL_DEBUG_CONFIG_CODE_OBJ_SHM;
    m_configMap[HSAIL_DEBUG_CONFIG_CODE_OBJ_SHM].param.shmemParam.m_shmKey = g_DBEBINARY_SHMKEY;
    m_configMap[HSAIL_DEBUG_CONFIG_CODE_OBJ_SHM].param.shmemParam.m_maxSize = g_BINARY_BUFFER_MAXSIZE;

    m_configMap[HSAIL_DEBUG_CONFIG_ISA_BUFFER_SHM].paramType = HSAIL_DEBUG_CONFIG_ISA_BUFFER_SHM;
    m_configMap[HSAIL_DEBUG_CONFIG_ISA_BUFFER_SHM].param.shmemParam.m_shmKey = g_ISASTREAM_SHMKEY;
    m_configMap[HSAIL_DEBUG_CONFIG_ISA_BUFFER_SHM].param.shmemParam.m_maxSize = g_ISASTREAM_MAXSIZE;

    m_configMap[HSAIL_DEBUG_CONFIG_MOMENTARY_BP_SHM].paramType = HSAIL_DEBUG_CONFIG_MOMENTARY_BP_SHM;
    m_configMap[HSAIL_DEBUG_CONFIG_MOMENTARY_BP_SHM].param.shmemParam.m_shmKey = g_MOMENTARY_BP_BUFFER_SHMKEY;
    m_configMap[HSAIL_DEBUG_CONFIG_MOMENTARY_BP_SHM].param.shmemParam.m_maxSize = g_MOMENTARY_BP_BUFFER_MAXSIZE;

    m_configMap[HSAIL_DEBUG_CONFIG_WAVE_INFO_SHM].paramType = HSAIL_DEBUG_CONFIG_WAVE_INFO_SHM;
    m_configMap[HSAIL_DEBUG_CONFIG_WAVE_INFO_SHM].param.shmemParam.m_shmKey = g_WAVE_BUFFER_SHMKEY;
    m_configMap[HSAIL_DEBUG_CONFIG_WAVE_INFO_SHM].param.shmemParam.m_maxSize = g_WAVE_BUFFER_MAXSIZE;

    m_configMap[HSAIL_DEBUG_CONFIG_LOADMAP_BUFFER_SHM].paramType = HSAIL_DEBUG_CONFIG_LOADMAP_BUFFER_SHM;
    m_configMap[HSAIL_DEBUG_CONFIG_LOADMAP_BUFFER_SHM].param.shmemParam.m_shmKey = g_LOADMAP_SHMKEY;
    m_configMap[HSAIL_DEBUG_CONFIG_LOADMAP_BUFFER_SHM].param.shmemParam.m_maxSize = g_LOADMAP_MAXSIZE;


    retCode = true;

    // For each parameter, check that it is unique
    return retCode;
}

}
