//==============================================================================
// Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Description: Logging interface for debug facilities
//==============================================================================

bool g_isLoggingEnabled = false;

void hwdbginfo_enable_logging()
{
    g_isLoggingEnabled = true;

}

void hwdbginfo_disable_logging()
{
    g_isLoggingEnabled = false;
}
