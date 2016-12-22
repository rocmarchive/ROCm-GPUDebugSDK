//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Functionality to manage the ISA buffers generated by the finalizer
//==============================================================================
#include <unistd.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <string>
#include <iomanip>

#include "AgentConfiguration.h"
#include "AgentISABuffer.h"
#include "AgentLogging.h"
#include "AgentNotifyGdb.h"
#include "AgentUtils.h"
#include "CommunicationControl.h"
#include "CommunicationParams.h"
#include "HSADebugAgent.h"

namespace HwDbgAgent
{

AgentISABuffer::~AgentISABuffer()
{
    if (m_pISABufferText != nullptr)
    {
        delete [] m_pISABufferText;
    }
}

AgentISABuffer::AgentISABuffer():
                    m_pISABufferText(NULL),
                    m_ISABufferLen()
{
}

HsailAgentStatus AgentISABuffer::WriteToSharedMem(const int isaBufferShmKey, const size_t isaBufferShmSize) const
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    // Get the pointer to the shmem segment
    void* pShm = AgentMapSharedMemBuffer(isaBufferShmKey, isaBufferShmSize);

    if (pShm == (int*) - 1)
    {
        AGENT_ERROR("WriteBinaryToShmem: Error with AgentMapSharedMemBuffer");
        return status;
    }

    // Clear the memory first
    memset(pShm, 0, isaBufferShmSize);
    // Write the size first
    size_t* pShmSizeLocation = (size_t*)pShm;
    pShmSizeLocation[0] = m_ISABufferLen;

    AGENT_LOG("ISA size: " << pShmSizeLocation[0]);

    if (m_pISABufferText == nullptr)
    {
        AGENT_LOG("No valid ISA buffer present");
        if (m_ISABufferLen != 0)
        {
            AGENT_ERROR("The ISA buffer len is non-zero but the buffer is nullptr");
        }
    }
    else
    {
        // Write the binary after the size_t info
        void* pShmBinaryLocation = (size_t*)pShm + 1;

        if (m_ISABufferLen < isaBufferShmSize )
        {
            // Copy the binary
            memcpy(pShmBinaryLocation, m_pISABufferText, m_ISABufferLen);
        }
        else
        {
            AGENT_WARNING("WriteToSharedMem: ISA Buffer could not be copied to GDB");
            AGENT_WARNING("Binary Size is = " << m_ISABufferLen <<
                          " but shared memory size = " << isaBufferShmSize << "bytes");
        }
    }

    status = AgentUnMapSharedMemBuffer(pShm);

    return status;
}

bool AgentISABuffer::TestForAMDHsaCod()
{
    bool retCode = false;

    if (system (NULL) == 0)
    {
        int noTermErrno = errno;
        AGENT_ERROR("Cannot call system(), errno: " << noTermErrno << ", " << strerror(noTermErrno));
        return retCode;
    }

    const std::string whichAmdhsacod = "which amdhsacod > /dev/null";
    AGENT_LOG("TestForAMDHsaCod: Call " << whichAmdhsacod);

    int ret_value = system(whichAmdhsacod.c_str());
    int err_no = errno;
    AGENT_LOG("TestForAMDHsaCod: Return code: " << retCode << " errno: " << strerror(err_no));

    if (ret_value == 0)
    {
        retCode = true;
    }

    return retCode;
}

HsailAgentStatus  AgentISABuffer::DisassembleAMDHsaCod(const size_t size, const void* codeObj)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    const std::string amdhsaCodCommand = "amdhsacod -dump -code";
    const std::string codeObjFilename  = "/tmp/codeobj";
    std::string isatextFilename(gs_ISAFileNamePath);

    if (size <= 0 || codeObj == nullptr )
    {
        AGENT_ERROR("DisassembleAMDHsaCod: Invalid input");
        return status;
    }

    status =  AgentWriteBinaryToFile(codeObj, size, codeObjFilename.c_str());
    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not save the code object to disassemble ISA");
        return status;
    }

    if (!TestForAMDHsaCod())
    {
        AGENT_ERROR("Could not find amdhsacod, kernels cannot be disassembled");
        return status;
    }

    // The command to call amdhsacod is
    // amdhsacod -dump -code CodeObjFileName > IsaFile
    // Using ">" when redirecting will clear the file before writing
    std::stringstream disassembleCommand;
    disassembleCommand << amdhsaCodCommand << " " << codeObjFilename << " > " << isatextFilename;

    AGENT_LOG("DisassembleCodeObject: Call " << disassembleCommand.str());

    int retCode = system(disassembleCommand.str().c_str());
    int err_no = errno;
    AGENT_LOG("DisassembleCodeObject: Return code: " << retCode << "errno: " << strerror(err_no));

    if (retCode != 0)
    {
        AGENT_ERROR("Could not disassemble successfully");
    }

    status = AgentDeleteFile(codeObjFilename.c_str());
    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not delete " << codeObjFilename);
    }

    return status;
}


HsailAgentStatus  AgentISABuffer::DisassembleLLVMObjDump(const size_t size, const void* codeObj)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    //We first look for the hcc-lc/llvm since we know that version supports gcn
    const std::string LLVM_CMD_OPTION1 = "/opt/rocm/hcc-lc/llvm/bin/llvm-objdump";

    // In future hcc versions, the objdump that supports gcn will be in hcc-lc/compiler and
    // option1 wont exist
    const std::string LLVM_CMD_OPTION2 = "/opt/rocm/hcc-lc/compiler/bin/llvm-objdump";

    std::string llvmCmdFileNameToUse("");

    const std::string llvmCmdOptions = "-disassemble -arch=amdgcn  -mcpu=fiji";
    const std::string codeObjFilename  = "/tmp/codeobj";
    std::string isatextFilename(gs_ISAFileNamePath);

    if (size <= 0 || codeObj == nullptr )
    {
        AGENT_ERROR("DisassembleLLVMObjDump: Invalid input");
        return status;
    }

    if (AgentIsFileExists(LLVM_CMD_OPTION1.c_str()))
    {
        llvmCmdFileNameToUse.append(LLVM_CMD_OPTION1);
    }
    else if (AgentIsFileExists(LLVM_CMD_OPTION2.c_str()))
    {
        llvmCmdFileNameToUse.append(LLVM_CMD_OPTION2);
    }
    else
    {
        AGENT_ERROR("DisassembleLLVMObjDump: Could not find llvm-objdump");
        return status;
    }

    status =  AgentWriteBinaryToFile(codeObj, size, codeObjFilename.c_str());
    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not save the code object to disassemble ISA");
        return status;
    }

    // The command to call llvm-objdump is
    // llvm-objdump -disassemble -arch=amdgcn  -mcpu=fiji codeobj.bin
    // Using ">" when redirecting will clear the file before writing

    std::stringstream disassembleCommand;
    disassembleCommand.clear();
    disassembleCommand << llvmCmdFileNameToUse << " "
                       << llvmCmdOptions << " "
                       << codeObjFilename << " > "
                       << isatextFilename;

    AGENT_LOG("DisassembleCodeObject: Call " << disassembleCommand.str());

    int retCode = system(disassembleCommand.str().c_str());
    int err_no = errno;
    AGENT_LOG("DisassembleCodeObject: Return code: " << retCode << "errno: " << strerror(err_no));

    if (retCode != 0)
    {
        AGENT_ERROR("Could not disassemble successfully");
    }

    status = AgentDeleteFile(codeObjFilename.c_str());
    if (status != HSAIL_AGENT_STATUS_SUCCESS)
    {
        AGENT_ERROR("Could not delete " << codeObjFilename);
    }

    return status;
}

HsailAgentStatus AgentISABuffer::PopulateISAFromFile(const std::string& ipFileName)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    // The input filename is the  ISA file name provided by the finalizer
    if(ipFileName.empty())
    {
        AGENT_ERROR("PopulateISAFromFile: Empty input filename")
        return status;
    }

    std::ifstream ipStream;

    ipStream.open(ipFileName.c_str(), std::ofstream::in);

    if (ipStream.is_open())
    {
        ipStream.seekg(0, ipStream.end);
        m_ISABufferLen = ipStream.tellg();

        AGENT_LOG("ISA buffer size: " << m_ISABufferLen);

        if (m_ISABufferLen > 0)
        {
            m_pISABufferText = new(std::nothrow) char[m_ISABufferLen];
        }

        if (m_pISABufferText == nullptr)
        {
            AGENT_ERROR("Could not allocate a buffer of size " << m_ISABufferLen);
            return status;
        }

        ipStream.seekg(0, ipStream.beg);
        ipStream.read(m_pISABufferText, m_ISABufferLen);

        AGENT_LOG("Save ISA from " << ipFileName);

        ipStream.close();
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }
    else
    {
        AGENT_ERROR("Could not open ISA file " << ipFileName);
    }

    return status;
}

HsailAgentStatus AgentISABuffer::PopulateISAFromCodeObj(const size_t size, const void* codeObj)
{
    // Use amdhsacod
    //HsailAgentStatus status = DisassembleAMDHsaCod(size, codeObj);

    // Use LLVM tools
    HsailAgentStatus status = DisassembleLLVMObjDump(size, codeObj);
    return status;
}

bool AgentISABuffer::CheckForKernelName(const std::string& kernelName) const
{
    bool retCode = false;

    std::string pattern = "AMD Kernel Code for " + kernelName+ ":";

    std::string buffer;
    if (m_pISABufferText == nullptr)
    {
        return retCode;
    }
    else
    {
        buffer.assign(m_pISABufferText);
    }

    std::size_t position = buffer.find(pattern);

    if (position != std::string::npos)
    {
        retCode = true;
    }

    AGENT_LOG("Look for pattern \"" << pattern << "\"");

    return retCode;
}

}
