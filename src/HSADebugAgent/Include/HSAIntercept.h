//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Intercepting HSA functions
//==============================================================================
#ifndef HSA_INTERCEPT_H_
#define HSA_INTERCEPT_H_

#include <hsa_api_trace.h>
#include "HSAAPITable1_0.h"

#include "CommunicationControl.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

HsailAgentStatus InitHsaCoreAgentIntercept(HsaApiTable* table);
HsailAgentStatus InitHsaCoreAgentIntercept1_0(ApiTable1_0* table);

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif // HSA_INTERCEPT_H
