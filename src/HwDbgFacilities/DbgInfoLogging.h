//==============================================================================
// Copyright (c) 2016-2017 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Description: Logging interface for debug facilities
//==============================================================================
#ifndef DBGINFOLOGGING_H_
#define DBGINFOLOGGING_H_

#include <cstring>
#include <sstream>
#include <string>

#include "DbgInfoUtils.h"

/// Write the log message
void hwdbginfo_log(const char* msg);

#define DBGINFO_LOG(stream)              \
{                                        \
    std::stringstream buffer;            \
    buffer.str("");                      \
    buffer << basename(__FILE__) << ":" << __LINE__  << ":  " << stream << "\n";            \
    hwdbginfo_log(buffer.str().c_str()); \
}                                        \

/// Set logging flag
void hwdbginfo_enable_logging();

/// Disable logging flag
void hwdbginfo_disable_logging();

#endif
