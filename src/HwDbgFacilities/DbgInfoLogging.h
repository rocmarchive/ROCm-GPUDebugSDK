//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Description: Logging interface for debug facilities
//==============================================================================
#include <iostream>

extern bool g_isLoggingEnabled;

#define DBGINFO_LOG(stream) \
{                           \
    if(g_isLoggingEnabled)  \
    {                       \
    std::cout << stream;    \
    }                       \
}                           \

/// Set logging flag
void hwdbginfo_enable_logging();

/// Disable logging flag
void hwdbginfo_disable_logging();
