//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Class to manage the agent binary
//==============================================================================
#include <cassert>
#include <cstring>
#include <string>
#include <cstdlib>
#include <errno.h>
#include <fstream>
#include <iostream>

#include <hsa_ext_amd.h>
#include <amd_hsa_kernel_code.h>

#include <libelf.h>

#include "AgentBinary.h"
#include "AgentConfiguration.h"
#include "AgentISABuffer.h"
#include "AgentLogging.h"
#include "AgentNotifyGdb.h"
#include "AgentUtils.h"
#include "CommunicationControl.h"
#include "HSADebugAgent.h"

#include "AMDGPUDebug.h"

namespace HwDbgAgent
{

/// Default constructor
AgentBinary::AgentBinary():
    m_pBinary(nullptr),
    m_binarySize(0),
    m_kernelName(""),
    m_codeObjBufferShmKey(-1),
    m_codeObjBufferMaxSize(0),
    m_pIsaBuffer(nullptr),
    m_enableISADisassemble(true)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    status = GetActiveAgentConfig()->GetConfigShmKey(HSAIL_DEBUG_CONFIG_CODE_OBJ_SHM, m_codeObjBufferShmKey);
    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not get shared mem key");
    }
    status = GetActiveAgentConfig()->GetConfigShmSize(HSAIL_DEBUG_CONFIG_CODE_OBJ_SHM, m_codeObjBufferMaxSize);
    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not get shared mem max size");
    }

    m_pIsaBuffer = new (std::nothrow) AgentISABuffer;
    if (m_pIsaBuffer == nullptr)
    {
        AGENT_ERROR("Could not allocate the ISA buffer");
    }

    char* pDisableISAEnvVar = nullptr;
    pDisableISAEnvVar = std::getenv("ROCM_GDB_DISABLE_ISA_DISASSEMBLE");
    if (pDisableISAEnvVar != nullptr)
    {
        AGENT_LOG("Disable GPU ISA disassemble," <<
                  " ROCM_GDB_DISABLE_ISA_DISASSEMBLE = " << pDisableISAEnvVar);
        AGENT_OP("Disable GPU ISA disassemble," <<
                  " ROCM_GDB_DISABLE_ISA_DISASSEMBLE = " << pDisableISAEnvVar);

        m_enableISADisassemble = false;
    }
}

AgentBinary::~AgentBinary()
{
    if(m_pIsaBuffer != nullptr)
    {
        delete m_pIsaBuffer;
        m_pIsaBuffer = nullptr;
    }
}

/// Demangle the input kernel name
HsailAgentStatus AgentBinary::DemangleKernelName(const std::string& ipKernelName,
                                                       std::string& demangledNameOut) const
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    // The strings are saved to a file and then piped in and out to c++filt
    const std::string MANGLED_STRING_FILE="/tmp/mangled_kernel";
    const std::string DEMANGLED_STRING_FILE="/tmp/demangled_kernel";


    if (ipKernelName.empty())
    {
        AGENT_ERROR("Input kernel name to Demangle function is empty");
        return status;
    }

    demangledNameOut.clear();

    // Just use the mangled kernel name if C++filt is not on system
    // The below check for a non-zero is correct.
    // if c++filt is not found, which will return some positive number
    if (system("which c++filt > /dev/null 2>&1"))
    {
        int err_no = errno;
        AGENT_LOG("DemangleKernelName: errno of which c++filt: " << err_no
                    << "errno: " << strerror(err_no));

        demangledNameOut.assign(ipKernelName);
        AGENT_OP("c++filt could not be found in the PATH, kernel names will remain mangled");
        status = HSAIL_AGENT_STATUS_SUCCESS;
        return status;
    }

    std::ofstream out(MANGLED_STRING_FILE, std::ofstream::out);
    if (!out.is_open())
    {
        AGENT_LOG("Mangled kernel file " << MANGLED_STRING_FILE << "could not be opened");
        return status;
    }
    else
    {
        std::string ipKernelNameWithUnderscore(ipKernelName);

        // If there is no underscore in the beginning, add one and then call c++filt
        // This is a work-around to a runtime issue where the first character of the
        // mangled name is missing, based on name mangling conventions in C++
        // the first 2 characters are _Z
        if(ipKernelName[0] == 'Z')
        {
            std::string::iterator it;
            it = ipKernelNameWithUnderscore.insert(ipKernelNameWithUnderscore.begin(),'_');
        }

        AGENT_LOG("Kernel name passed to c++filt " << ipKernelNameWithUnderscore);

        out << ipKernelNameWithUnderscore;
        out.close();

        std::stringstream demangleCommand;
        // The -p option removes the parameter list from the demangled name
        demangleCommand << "c++filt -p"<< " < " << MANGLED_STRING_FILE << " > " << DEMANGLED_STRING_FILE;
        int retCode = system(demangleCommand.str().c_str());
        int err_no = errno;
        AGENT_LOG("DemangleKernelName: Return code: " << retCode << "errno: " << strerror(err_no));


        std::ifstream inputStream(DEMANGLED_STRING_FILE, std::ifstream::in);
        if (inputStream.is_open())
        {
            getline(inputStream, demangledNameOut);
            status = HSAIL_AGENT_STATUS_SUCCESS;

            AGENT_LOG("Demangled kernel name: " << demangledNameOut);

            inputStream.close();

            // Delete tmp files
            AgentDeleteFile(MANGLED_STRING_FILE.c_str());
            AgentDeleteFile(DEMANGLED_STRING_FILE.c_str());
        }
    }
    return status;
}

// Call the DBE and set up the buffer
HsailAgentStatus AgentBinary::PopulateBinaryFromDBE(HwDbgContextHandle dbgContextHandle,
                                                    const hsa_kernel_dispatch_packet_t* pAqlPacket)
{
    AGENT_LOG("Initialize a new binary");
    assert(dbgContextHandle != nullptr);

    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    if (nullptr == dbgContextHandle)
    {
        AGENT_ERROR("Invalid DBE Context handle");
        return status;
    }

    // Note: Even though the DBE only gets a pointer for the binary,
    // the size of the binary is generated by the HwDbgHSAContext
    // by using ACL

    // Get the debugged kernel binary from DBE
    // A pointer to constant data
    HwDbgStatus dbeStatus = HwDbgGetKernelBinary(dbgContextHandle,
                                                 &m_pBinary,
                                                 &m_binarySize);

    assert(dbeStatus == HWDBG_STATUS_SUCCESS);
    assert(m_pBinary != nullptr);

    if (dbeStatus != HWDBG_STATUS_SUCCESS ||
        m_pBinary == nullptr)
    {
        AGENT_ERROR(GetDBEStatusString(dbeStatus) <<
                    "PopulateBinaryFromDBE: Error in HwDbgGeShaderBinary");

        // Something was wrong we should exit without writing the binary
        status = HSAIL_AGENT_STATUS_FAILURE;
        return status;
    }
    else
    {
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }

    // get the kernel name for the active dispatch
    std::string demangledKernelName;
    const char* pMangledKernelName(nullptr);
    dbeStatus = HwDbgGetDispatchedKernelName(dbgContextHandle, &pMangledKernelName);
    assert(dbeStatus == HWDBG_STATUS_SUCCESS);
    assert(pMangledKernelName != nullptr);

    if (dbeStatus != HWDBG_STATUS_SUCCESS ||
        pMangledKernelName == nullptr)
    {
        AGENT_ERROR("PopulateBinaryFromDBE: Could not get the name of the kernel");
        status = HSAIL_AGENT_STATUS_FAILURE;
        return status;
    }
    else
    {
        std::string mangledKernelName(pMangledKernelName);
        AGENT_LOG("Mangled Kernel name " << mangledKernelName);
        status =  DemangleKernelName(mangledKernelName, demangledKernelName);
    }


    m_kernelName.assign(demangledKernelName);

    AGENT_LOG("PopulateBinaryFromDBE: Kernel Name found " << m_kernelName);

    if (m_enableISADisassemble)
    {
        m_pIsaBuffer->PopulateISAFromCodeObj(m_binarySize, m_pBinary);
    }

    return status;
}

const std::string AgentBinary::GetKernelName()const
{
    return m_kernelName;
}

/// Validate parameters of the binary, write the binary to shmem
/// and let gdb know we have a new binary
HsailAgentStatus AgentBinary::NotifyGDB(const hsa_kernel_dispatch_packet_t* pAqlPacket,
                                        const uint64_t                      queueID,
                                        const uint64_t                      packetID) const
{

    HsailAgentStatus status;

    // Check that kernel name is not empty
    if (m_kernelName.empty())
    {
        AGENT_LOG("NotifyGDB: Kernel name may not have not been populated");
    }

    // Call function in AgentNotify
    // Let gdb know we have a new binary
    status = WriteBinaryToSharedMem();

    if (HSAIL_AGENT_STATUS_FAILURE == status)
    {
        AGENT_ERROR("NotifyGDB: Could not write binary to shared mem");
        return status;
    }

    status = AgentNotifyNewBinary(m_binarySize,
                                  m_kernelName,
                                  pAqlPacket,
                                  queueID,
                                  packetID);

    if (HSAIL_AGENT_STATUS_FAILURE == status)
    {
        AGENT_ERROR("NotifyGDB: Couldnt not notify gdb");
    }

    return status;
}

HsailAgentStatus AgentBinary::WriteBinaryToSharedMem() const
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    if (m_pBinary == nullptr)
    {
        AGENT_ERROR("WriteBinaryToShmem: Error Binary is null");
        return status;
    }

    if (m_binarySize <= 0)
    {
        AGENT_ERROR("WriteBinaryToShmem: Error Binary size is 0");
        return status;
    }

    if (m_binarySize > m_codeObjBufferMaxSize)
    {
        AGENT_ERROR("WriteBinaryToShmem: Error Binary is larger than the shared mem allocated");
        return status;
    }

    // The shared mem segment needs place for a size_t value and the binary
    if ((m_binarySize + sizeof(size_t)) > m_codeObjBufferMaxSize)
    {
        AGENT_ERROR("WriteBinaryToShmem: Binary size is too big");
        return status;
    }

    // Get the pointer to the shmem segment
    void* pShm = AgentMapSharedMemBuffer(m_codeObjBufferShmKey, m_codeObjBufferMaxSize);

    if (pShm == (int*) - 1)
    {
        AGENT_ERROR("WriteBinaryToShmem: Error with AgentMapSharedMemBuffer");
        return status;
    }

    // Clear the memory fist
    memset(pShm, 0, m_codeObjBufferMaxSize);

    // Write the size first
    size_t* pShmSizeLocation = (size_t*)pShm;
    pShmSizeLocation[0] = m_binarySize;

    AGENT_LOG("DBE Code object size: " << pShmSizeLocation[0]);

    // Write the binary after the size_t info
    void* pShmBinaryLocation = (size_t*)pShm + 1;
    if (m_binarySize < m_codeObjBufferMaxSize)
    {
        // Copy the binary
        memcpy(pShmBinaryLocation, m_pBinary, m_binarySize);
    }
    else
    {
        AGENT_WARNING("WriteBinaryToSharedMem: Did not copy code object to shared memory");
        AGENT_WARNING("Binary Size is =" << pShmSizeLocation[0] <<
                      " but shared memory size = " << m_codeObjBufferMaxSize << "bytes");
    }

    //printf("Debug OP");
    //for(int i = 0; i<10;i++)
    //{
    //    printf("%d \t %d\n",i,*((int*)pShmBinaryLocation + i));
    //}

    // Detach shared memory
    status = AgentUnMapSharedMemBuffer(pShm);

    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("WriteBinaryToShmem: Error with AgentUnMapSharedMemBuffer");
        return status;
    }

    return status;
}

HsailAgentStatus AgentBinary::WriteBinaryToFile(const char* pFilename) const
{

    HsailAgentStatus status = AgentWriteBinaryToFile(m_pBinary, m_binarySize, pFilename);

    if (status == HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_LOG("DBE Binary Saved to " << pFilename);
    }
    return status;
}
} // End Namespace HwDbgAgent
