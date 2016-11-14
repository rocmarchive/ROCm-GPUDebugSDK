//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Agent Segment Loader
//==============================================================================
#include <iostream>
#include <cstring>
#include <libelf.h>

#include "AgentConfiguration.h"
#include "AgentLogging.h"
#include "AgentSegmentLoader.h"
#include "CommunicationControl.h"
#include "HSADebugAgent.h"

namespace HwDbgAgent
{

AgentSegmentLoader::AgentSegmentLoader(hsa_kernel_dispatch_packet_t* pAqlPacket):
                     m_pLoadedSegments(nullptr),
                     m_numLoadedSegments(0),
                     m_loadedSegmentShmKey(-1),
                     m_loadedSegmentShmMaxSize(0),
                     m_kernelObjectAddress(0)
{
    GetActiveAgentConfig()->GetConfigShmKey(HSAIL_DEBUG_CONFIG_LOADMAP_BUFFER_SHM, m_loadedSegmentShmKey);
    GetActiveAgentConfig()->GetConfigShmSize(HSAIL_DEBUG_CONFIG_LOADMAP_BUFFER_SHM, m_loadedSegmentShmMaxSize);
    if (pAqlPacket != nullptr)
    {
        m_kernelObjectAddress = pAqlPacket->kernel_object;
    }
}

AgentSegmentLoader::~AgentSegmentLoader()
{
    ClearLoadedSegments();
}

size_t AgentSegmentLoader::FindExecutedSegment() const
{
    if (0 == m_numLoadedSegments || nullptr == m_pLoadedSegments)
    {
        return SIZE_MAX;
    }

    for (size_t i = 0; i < m_numLoadedSegments; ++i)
    {
        uint64_t segmentAddress = reinterpret_cast<uint64_t>(m_pLoadedSegments[i].pSegmentBase);
        uint64_t segmentSize = static_cast<uint64_t>(m_pLoadedSegments[i].segmentSize);


        if ( (m_kernelObjectAddress <= (segmentAddress + segmentSize)) &&
             (m_kernelObjectAddress >= segmentAddress ))
        {
            return i;
        }
    }

    return SIZE_MAX;
}

const HwDbgLoaderSegmentDescriptor* AgentSegmentLoader::GetLoadedSegmentBuffer() const
{
    return m_pLoadedSegments;
}
const size_t AgentSegmentLoader::GetNumLoadedSegments() const
{
    return m_numLoadedSegments;
}

HsailAgentStatus AgentSegmentLoader::UpdateLoadedSegments()
{
    ClearLoadedSegments();

    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    // Just test the load map
    size_t numSegments=0;
    HwDbgStatus dbeStatus  = HwDbgGetLoadedSegmentDescriptors(nullptr, &numSegments);
    if ((numSegments == 0) && (dbeStatus == HWDBG_STATUS_SUCCESS))
    {
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }
    else if ((0 < numSegments) && (dbeStatus == HWDBG_STATUS_SUCCESS))
    {

        m_pLoadedSegments = new (std::nothrow) HwDbgLoaderSegmentDescriptor[numSegments];

        dbeStatus = HwDbgGetLoadedSegmentDescriptors(m_pLoadedSegments, &numSegments);
        if (dbeStatus == HWDBG_STATUS_SUCCESS)
        {
            m_numLoadedSegments = numSegments;

            status = HSAIL_AGENT_STATUS_SUCCESS;
        }


        status = WriteToSharedMemory();
    }


    return status;
}

HsailAgentStatus AgentSegmentLoader::WriteToSharedMemory() const
{

    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    if (m_pLoadedSegments == nullptr || m_numLoadedSegments == 0)
    {
        return status;
    }
    if (sizeof(HsailSegmentDescriptor)*m_numLoadedSegments > m_loadedSegmentShmMaxSize)
    {
        AGENT_ERROR("Too many segments to send to gdb");
        return status;
    }
    void* pShm = AgentMapSharedMemBuffer(m_loadedSegmentShmKey, m_loadedSegmentShmMaxSize);
    memset(pShm,0,m_loadedSegmentShmMaxSize);

    if (pShm != nullptr)
    {
        size_t* pNumLoaded = reinterpret_cast<size_t*>(pShm);
        *pNumLoaded = m_numLoadedSegments;
        HsailSegmentDescriptor* pSegmentMem = reinterpret_cast<HsailSegmentDescriptor*>(pNumLoaded + 1);
        for (size_t i=0; i < m_numLoadedSegments; i++)
        {
            pSegmentMem[i].codeObjectStorageBase = reinterpret_cast<size_t>(m_pLoadedSegments[i].pCodeObjectStorageBase);
            pSegmentMem[i].codeObjectStorageOffset =  m_pLoadedSegments[i].codeObjectStorageOffset;
            pSegmentMem[i].codeObjectStorageType = static_cast<HsailLoaderCodeObjectStorageType>(m_pLoadedSegments[i].codeObjectStorageType);
            pSegmentMem[i].codeObjectStorageSize = m_pLoadedSegments[i].codeObjectStorageSize;
            pSegmentMem[i].device = m_pLoadedSegments[i].device;
            pSegmentMem[i].executable = m_pLoadedSegments[i].executable;
            pSegmentMem[i].segmentBase = reinterpret_cast<size_t>(m_pLoadedSegments[i].pSegmentBase);
            pSegmentMem[i].segmentSize = m_pLoadedSegments[i].segmentSize;
        }

        AddElfVAForEachSegmentDescriptor(pSegmentMem);

        size_t executedSegmentIndex = FindExecutedSegment();
        if  (executedSegmentIndex != SIZE_MAX)
        {
            pSegmentMem[executedSegmentIndex].isSegmentExecuted = true;
        }

        AgentLogLoadMap(pSegmentMem, m_numLoadedSegments);

        status = AgentUnMapSharedMemBuffer(pShm);

    }

    return status;
}

void AgentSegmentLoader::AddElfVAForEachSegmentDescriptor(HsailSegmentDescriptor* pSegments) const
{

    if (pSegments != nullptr && m_numLoadedSegments > 0)
    {
        // Get elf structure at the start of the file, null checked above
        const Elf64_Ehdr* pElfEhDr = static_cast<const Elf64_Ehdr*>(m_pLoadedSegments->pCodeObjectStorageBase);
        size_t phdrOffsetinBytes = static_cast<size_t>(pElfEhDr->e_phoff);

        // Get location of the list of pPhDrs array within the CodeObject ELF
        Elf64_Phdr* pPhdrList = (Elf64_Phdr*)(
                                    (char*)m_pLoadedSegments->pCodeObjectStorageBase + phdrOffsetinBytes);

        // Size of the pPhDrs array, obtained from the elf structure at the start of the file
        size_t numTotalPhdrs = static_cast<size_t>(pElfEhDr->e_phnum );

        // For each of the input descriptors
        for (size_t i=0; i< m_numLoadedSegments; i++)
        {
            // For each loaded GPU section, we find the corresponding program header
            // The program header needs to be of type "LOAD" which is = 1 && the offset must match
            bool isSegmentFound = false;
            for (size_t j=0; j < numTotalPhdrs; j++)
            {
                if ((pPhdrList[j].p_offset == m_pLoadedSegments[i].codeObjectStorageOffset))
                    //&&
                    //(pPhdrList[j].p_type == 1))
                {
                    pSegments[i].segmentBaseElfVA = static_cast<uint64_t>(pPhdrList[j].p_vaddr);
                    isSegmentFound = true;
                    break;
                }
            }
            if (!isSegmentFound)
            {
                // The AgentLog function that logs the loadmap has more details
                AGENT_LOG("Segment " << i << " elf VA could not be found");
            }
        }
    }
}
void AgentSegmentLoader::ClearLoadedSegments()
{
    if (m_pLoadedSegments != nullptr)
    {
        delete [] m_pLoadedSegments;
        m_numLoadedSegments = 0;
    }
}

}
