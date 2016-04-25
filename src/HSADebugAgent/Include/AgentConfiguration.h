//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Agent side Implementation of the Hsail-gdb configuration manager
//==============================================================================
#ifndef AGENT_CONFIG_H_
#define AGENT_CONFIG_H_

#include <string>
#include <map>

#include "CommunicationControl.h"

namespace HwDbgAgent
{
// Handles the configuration parameters shared between GDB and the Agent
class AgentConfiguration
{
public:

    /// Constructor
    AgentConfiguration();

    /// Destructor
    ~AgentConfiguration()
    {
        m_configMap.clear();
    }

    /// Set up the agent configuration, presently only reading the default shared header is supported
    HsailAgentStatus ConfigureAgent();

    /// Query APIs to get configuration parameters, used for int types such as shared memory key
    /// \param[in]  requestedParam  The requested parameter
    /// \param[out] outKey          The output data about the requested parameter
    HsailAgentStatus GetConfigShmKey(const HsailDebugConfigParam requestedParam,
                                           int&                  outKey) const;

    /// Query APIs to get configuration parameters, used for size_t types such as shared memory size data
    /// \param[in]  requestedParam  The requested parameter
    /// \param[out] outMaxSize      The output data about the requested parameter
    HsailAgentStatus GetConfigShmSize(const HsailDebugConfigParam requestedParam,
                                            size_t&               outMaxSize) const;

private:

    /// Map of each parameter and its data
    std::map <HsailDebugConfigParam, HsailConfigParam> m_configMap;

    /// Validate the file, we will only over-ride the defaults if every parameter is good
    bool ValidateFile() const;

    /// Configure the defaults, from the shared header.
    bool ReadDefaultConfiguration();

    /// The config file
    std::string m_configFileName;
};
}
#endif // AGENT_CONFIG_H_
