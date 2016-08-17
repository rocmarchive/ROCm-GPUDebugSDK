//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Utility functions for HSA Debug Agent
//==============================================================================
#ifndef AGENT_UTILS_H_
#define AGENT_UTILS_H_

#include <stdint.h>
#include <cstddef>
#include <vector>

#include <hsa.h>

#include "AMDGPUDebug.h"

static const HwDbgDim3 gs_UNKNOWN_HWDBGDIM3 = {uint32_t(-1), uint32_t(-1), uint32_t(-1)};

// A fatal exit function so that all the exit behavior can be handled in one place
void AgentFatalExit();

// Get symbols from an ELF binary
void ExtractSymbolListFromELFBinary(const void* pBinary,
                                    size_t binarySize,
                                    std::vector<std::pair<std::string, uint64_t>>& outputSymbols);

const std::string GetDBEEventString(const HwDbgEventType event);

const std::string GetDBEStatusString(const HwDbgStatus status);

const std::string GetCommandTypeString(const HsailCommand ipCommand);

std::string GetHsaStatusString(const hsa_status_t s);

/// Check for a valid HwDbgGetActiveWaves call and the corresponding wave data
/// Return false if any of the output from HwDbgGetActiveWaves is invalid
/// \param[in] dbeStatus         The DBE status
/// \param[in] nWaves            The number of waves reported by the HwDbgGetActiveWaves call
/// \param[in] pWaveInfo         The wave info buffer
/// \param[out] isBufferEmptyOut True if the buffer is empty, this is a valid possibility
bool AgentIsWaveInfoBufferValid(const HwDbgStatus         dbeStatus,
                                const uint32_t            nWaves,
                                const HwDbgWavefrontInfo* pWaveInfo,
                                      bool&               isBufferEmptyOut);

// Checks if the workgroup and workitem passed belong to the wavefront
bool AgentIsWorkItemPresentInWave(const HwDbgDim3&          workGroup,
                                  const HwDbgDim3&          workItem,
                                  const HwDbgWavefrontInfo* pWaveInfo);

HsailAgentStatus AgentLoadFileAsSharedObject(const std::string& ipFilename);

HsailAgentStatus AgentWriteISAToFile(const std::string&                  isaFileName,
                                           hsa_kernel_dispatch_packet_t* aql);

/// Delete a file
HsailAgentStatus AgentDeleteFile(const char* ipFilename);

/// Write a binary buffer to file
HsailAgentStatus AgentWriteBinaryToFile(const void*  pBinary, const size_t binarySize, const char*  pFilename);

bool AgentWriteDLLPathToString(const std::string& dllName, std::string& msg);

bool CompareHwDbgDim3(const HwDbgDim3& op1, const HwDbgDim3& op2);

void CopyHwDbgDim3(HwDbgDim3& dst, const HwDbgDim3& src);

HsailAgentStatus CopyAQLToHsailDispatch(      HsailDispatchPacket*          pOpPacket,
                                        const hsa_kernel_dispatch_packet_t* pAqlPacket);

bool ValidateAQL(const hsa_kernel_dispatch_packet_t& AQL);

#endif // AGENT_UTILS_H
