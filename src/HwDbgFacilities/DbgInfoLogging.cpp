//==============================================================================
// Copyright (c) 2016-2017 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Description: Logging interface for debug facilities
//==============================================================================
#include <iostream>

#include "DbgInfoLogging.h"

static bool gs_isLoggingEnabled = false;

void hwdbginfo_enable_logging()
{
    gs_isLoggingEnabled = true;

}

void hwdbginfo_disable_logging()
{
    gs_isLoggingEnabled = false;
}

void hwdbginfo_log(const char* msg)
{
    if(gs_isLoggingEnabled)
    {
        std::cout << msg;
    }
}
