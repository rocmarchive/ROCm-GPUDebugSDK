//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Agent Segment Loader
//==============================================================================
#include "AMDGPUDebug.h"

#include "AgentUtils.h"
#include "CommunicationControl.h"

namespace HwDbgAgent
{
class AgentSegmentLoader
{
public:

    AgentSegmentLoader(hsa_kernel_dispatch_packet_t* pAqlPacket);

    ~AgentSegmentLoader();

    const HwDbgLoaderSegmentDescriptor* GetLoadedSegmentBuffer() const;
    const size_t GetNumLoadedSegments() const;

    HsailAgentStatus UpdateLoadedSegments();

private:

    AgentSegmentLoader();

    void ClearLoadedSegments();

    HsailAgentStatus WriteToSharedMemory() const;

    size_t FindExecutedSegment() const;

    void AddElfVAForEachSegmentDescriptor(HsailSegmentDescriptor* pSegments) const;

    HwDbgLoaderSegmentDescriptor* m_pLoadedSegments;

    size_t m_numLoadedSegments;

    int m_loadedSegmentShmKey;

    size_t m_loadedSegmentShmMaxSize;

    uint64_t m_kernelObjectAddress;
};
}
