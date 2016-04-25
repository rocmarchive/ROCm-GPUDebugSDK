//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Implementation of AgentBinary
//==============================================================================
#ifndef AGENT_BINARY_H_
#define AGENT_BINARY_H_

#include <cstdint>
#include <string>

#include <hsa.h>

#include "AMDGPUDebug.h"
#include "CommunicationControl.h"

namespace HwDbgAgent
{

// forward declaration:
class AgentISABuffer;

/// A class that maintains a single binary from the debug back end library
/// Obtains the binary from the back end library and sends it to GDB
class AgentBinary
{
private:

    /// Binary from DBE - the memory for the binary is allocated by the DBE
    const void* m_pBinary;

    /// Size of binary
    size_t m_binarySize;

    /// Symbols we dig out of the binary
    std::string m_llSymbolName;
    std::string m_hlSymbolName;

    /// The dispatched kernel name
    std::string m_kernelName;

    /// Key for the code object buffer's shared memory
    int m_codeObjBufferShmKey;

    /// Max size for the code object buffer's shared memory
    size_t m_codeObjBufferMaxSize;

    AgentISABuffer* m_pIsaBuffer;

    bool m_enableISADisassemble;

    /// Disable copy constructor
    AgentBinary(const AgentBinary&);

    /// Disable assignment operator
    AgentBinary& operator=(const AgentBinary&);

    /// Get the HL and LL symbols
    bool GetDebugSymbolsFromBinary();

    /// Write the binary to shared mem
    /// \param[in] shmKey the key for shared memory used where the binary will be written  to
    HsailAgentStatus WriteBinaryToSharedMem() const;

public:
    /// Default constructor
    AgentBinary();

    /// Destructor
    ~AgentBinary();

    /// Call HwDbgGetShaderBinary and populate the object.
    /// Also extracts the relevant symbols from the binary.
    ///
    /// \param[in] ipHandle The DBE context handle
    /// \param[in] pAqlPacket The AQL packet for the dispatch
    /// \return HSAIL agent status
    HsailAgentStatus PopulateBinaryFromDBE(HwDbgContextHandle ipHandle, const hsa_kernel_dispatch_packet_t* pAqlPacket);

    /// Write the Notification payload to gdb
    HsailAgentStatus NotifyGDB(const hsa_kernel_dispatch_packet_t* pAqlPacket,
                               const uint64_t                      queueID,
                               const uint64_t                      packetID) const;

    /// Write the binary to a file, useful for debug
    /// \param[in] pFilenamePrefix Input filename prefix
    HsailAgentStatus WriteBinaryToFile(const char* pFilenamePrefix) const;

    /// Return the kernel name
    ///
    /// \return The kernel name for this code object
    const std::string GetKernelName() const;
};
} // End Namespace HwDbgAgent

#endif // AGENT_BINARY_H_
