//==============================================================================
// Copyright (c) 2012-2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Description: General functions
//==============================================================================
/// Local:
#include <DbgInfoUtils.h>

/// STL:
#include <string>
#include <memory>
#include <stdarg.h>
#include <string.h>

#include <cstdlib>

using namespace HwDbg;


//@{
/// Helper function, prints a formatted string into an std::string:
std::string HwDbg::string_format(const std::string fmt_str, ...)
{
    int final_n, n = ((int)fmt_str.size()) * 2; /* reserve 2 times as much as the length of the fmt_str */
    std::string str;
    std::unique_ptr<char[]> formatted;
    va_list ap;

    while (1)
    {
        formatted.reset(new char[n]); /* wrap the plain char array into the unique_ptr */
        ::strcpy(&formatted[0], fmt_str.c_str());
        va_start(ap, fmt_str);
        final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap);
        va_end(ap);

        if (final_n < 0 || final_n >= n)
        {
            n += abs(final_n - n + 1);
        }
        else
        {
            break;
        }
    }

    return std::string(formatted.get());
}
//@}

//@{
/// Helper function, prepends a string to another string:
std::string& HwDbg::string_prepend(std::string& str, std::string prefixStr)
{
    // Pass-by-value allows us to do this:
    prefixStr.append(str);
    str = prefixStr;
    return str;
}
//@}

//@{
/// Helper function, removes trailing characters:
std::string& HwDbg::string_remove_trailing(std::string& str, char c)
{
    if (!str.empty())
    {
        std::string::iterator startIter = str.begin();
        std::string::iterator endIter = str.end();

        // Look for the position of the last char that is not the input char:
        std::string::iterator iter = endIter;

        while (iter != startIter)
        {
            iter--;

            if (*iter != c)
            {
                break;
            }
        }

        // If there are trailing chars to be removed:
        if ((iter != endIter) && (iter + 1) != endIter)
        {
            // Remove them:
            str.erase(iter + 1, endIter);
        }
    }

    return str;
}
//@}


