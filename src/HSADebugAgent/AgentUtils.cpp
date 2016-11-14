//==============================================================================
// Copyright (c) 2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file  AgentUtils.cpp
/// \brief Utility functions for HSA Debug Agent
//==============================================================================
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <errno.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include <stdint.h>
#include <cstddef>

#include "AMDGPUDebug.h"

#include "AgentLogging.h"
#include "AgentUtils.h"

#include <libelf.h>

// Just a simple function so that all the exit behavior can be handled in one place
// We can add logging parameters but it is expected that you would call the logging
// functions before you fatally exit
// We will try to restrict this function's usage so that the process  dies only from
// errors in system calls
void AgentFatalExit()
{
    AgentErrorLog("FatalExit\n");
    exit(-1);
}

// return true if equal, else return false
bool CompareHwDbgDim3(const HwDbgDim3& op1, const HwDbgDim3& op2)
{
    if (op1.x == op2.x &&
        op1.y == op2.y &&
        op1.z == op2.z)
    {
        return true;
    }

    return false;
}

void PopulateHsailDim3(HsailWaveDim3& dst, const uint16_t x, const uint16_t y, const uint16_t z)
{
    dst.x = (uint32_t)x;
    dst.y = (uint32_t)y;
    dst.z = (uint32_t)z;
}

void CopyHwDbgDim3(HwDbgDim3& dst, const HwDbgDim3& src)
{
    dst.x = src.x;
    dst.y = src.y;
    dst.z = src.z;
}

static bool ValidateAQLDimensions(const uint32_t GRID_SIZE, const uint32_t WORK_GROUP_SIZE)
{
    bool ret = true;

    if (GRID_SIZE == 0)
    {
        AGENT_WARNING("AQL grid_size cannot be 0.");
        ret &= false;
    }

    if (WORK_GROUP_SIZE == 0)
    {
        AGENT_WARNING("AQL work_group_size cannot be 0.");
        ret &= false;
    }

    if (GRID_SIZE < WORK_GROUP_SIZE)
    {
        AGENT_WARNING("AQL grid_size " << GRID_SIZE << " shouldn't be less than work_group_size "
                      << WORK_GROUP_SIZE << ".");
        ret &= false;
    }

    return ret;
}

bool ValidateAQL(const hsa_kernel_dispatch_packet_t& AQL)
{
    // Check grid_size and work_group_size
    // We only give warning to the user if dimensions setup is incorrect.
    // The AQL packet would still be dispatched in this case.
    if (!ValidateAQLDimensions(AQL.grid_size_x, AQL.workgroup_size_x))
    {
        AGENT_WARNING("AQL dimension x setup incorrect.\n");
    }

    if (!ValidateAQLDimensions(AQL.grid_size_y, AQL.workgroup_size_y))
    {
        AGENT_WARNING("AQL dimension y setup incorrect.\n");
    }

    if (!ValidateAQLDimensions(AQL.grid_size_z, AQL.workgroup_size_z))
    {
        AGENT_WARNING("AQL dimension z setup incorrect.\n");
    }

    // TODO: check the rest of AQL field.

    return true;
}


HsailAgentStatus CopyAQLToHsailDispatch(HsailDispatchPacket* pOpPacket, const hsa_kernel_dispatch_packet_t* pAqlPacket)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    if (pOpPacket == nullptr || pAqlPacket == nullptr)
    {
        return status;
    }

    if (!ValidateAQL(*pAqlPacket))
    {
        return status;
    }
    memset(pOpPacket, 0, sizeof(HsailDispatchPacket));

    pOpPacket->header = pAqlPacket->header ;
    pOpPacket->setup = pAqlPacket->setup;

    PopulateHsailDim3(pOpPacket->workgroup_size,
                  pAqlPacket->workgroup_size_x, pAqlPacket->workgroup_size_y, pAqlPacket->workgroup_size_z);

    pOpPacket->reserved0 = pAqlPacket->reserved0;

    PopulateHsailDim3(pOpPacket->grid_size,
                  pAqlPacket->grid_size_x, pAqlPacket->grid_size_y, pAqlPacket->grid_size_z);

    pOpPacket->kernarg_address          = pAqlPacket->kernarg_address;
    pOpPacket->group_segment_size       = pAqlPacket->group_segment_size;
    pOpPacket->kernel_object            = pAqlPacket->kernel_object;
    pOpPacket->reserved2                = pAqlPacket->reserved2;
    pOpPacket->completion_signal_handle = (uint64_t)(pAqlPacket->completion_signal.handle) ;

    status = HSAIL_AGENT_STATUS_SUCCESS;
    return status;
}

/// Just print the packet type
const std::string GetCommandTypeString(const HsailCommand ipCommand)
{
    switch (ipCommand)
    {
        case HSAIL_COMMAND_UNKNOWN:
            return "HSAIL_COMMAND_UNKNOWN";

        case HSAIL_COMMAND_BEGIN_DEBUGGING:
            return "HSAIL_COMMAND_BEGIN_DEBUGGING";

        case HSAIL_COMMAND_CREATE_BREAKPOINT:
            return "HSAIL_COMMAND_CREATE_BREAKPOINT";

        case HSAIL_COMMAND_DELETE_BREAKPOINT:
            return "HSAIL_COMMAND_DELETE_BREAKPOINT";

        case HSAIL_COMMAND_ENABLE_BREAKPOINT:
            return "HSAIL_COMMAND_ENABLE_BREAKPOINT";

        case HSAIL_COMMAND_DISABLE_BREAKPOINT:
            return "HSAIL_COMMAND_DISABLE_BREAKPOINT";

        case HSAIL_COMMAND_MOMENTARY_BREAKPOINT:
            return "HSAIL_COMMAND_MOMENTARY_BREAKPOINT";

        case HSAIL_COMMAND_CONTINUE:
            return "HSAIL_COMMAND_CONTINUE";

        case HSAIL_COMMAND_SET_LOGGING:
            return "HSAIL_COMMAND_CONTINUE";

        default:
            return "[Unknown Command]";
    }
}

/// Just print the DBE event
const std::string GetDBEEventString(const HwDbgEventType event)
{
    switch (event)
    {
        case HWDBG_EVENT_POST_BREAKPOINT:
            return "HWDBG_EVENT_POST_BREAKPOINT";

        case HWDBG_EVENT_TIMEOUT:
            return "HWDBG_EVENT_TIMEOUT";

        case HWDBG_EVENT_END_DEBUGGING:
            return "HWDBG_EVENT_END_DEBUGGING";

        case HWDBG_EVENT_INVALID:
            return "HWDBG_EVENT_INVALID";

        default:
            return "Unknown HWDBG_EVENT";
    }
}

/// Just print the DBE string
const std::string GetDBEStatusString(const HwDbgStatus status)
{
    switch (status)
    {
        case HWDBG_STATUS_SUCCESS:
            return "DBE Status: HWDBG_STATUS_SUCCESS\n";

        case HWDBG_STATUS_ERROR:
            return "DBE Status: HWDBG_STATUS_ERROR\n";

        case HWDBG_STATUS_DEVICE_ERROR:
            return "DBE Status: HWDBG_STATUS_DEVICE_ERROR\n";

        case HWDBG_STATUS_INVALID_HANDLE:
            return "DBE Status: HWDBG_STATUS_INVALID_HANDLE\n";

        case HWDBG_STATUS_INVALID_PARAMETER:
            return "DBE Status: HWDBG_STATUS_INVALID_PARAMETER\n";

        case HWDBG_STATUS_NULL_POINTER:
            return "DBE Status: HWDBG_STATUS_NULL_POINTER\n";

        case HWDBG_STATUS_OUT_OF_MEMORY:
            return "DBE Status: HWDBG_STATUS_OUT_OF_MEMORY\n";

        case HWDBG_STATUS_OUT_OF_RESOURCES:
            return "DBE Status: HWDBG_STATUS_OUT_OF_RESOURCES\n";

        case HWDBG_STATUS_REGISTRATION_ERROR:
            return "DBE Status: HWDBG_STATUS_REGISTRATION_ERROR\n";

        case HWDBG_STATUS_UNDEFINED:
            return "DBE Status: HWDBG_STATUS_UNDEFINED\n";

        case HWDBG_STATUS_UNSUPPORTED:
            return "DBE Status: HWDBG_STATUS_UNSUPPORTED\n";

        // This should never happen since we covered the whole enum
        default:
            return "DBE Status: [Unknown DBE Printing]";
    }
}

std::string GetHsaStatusString(const hsa_status_t s)
{
    const char* pszbuff = { 0 };
    hsa_status_string(s, &pszbuff);

    std::string str = pszbuff;
    return str;
}

bool AgentIsFileExists(const char *fileName)
{
    std::ifstream infile(fileName);
    return infile.good();
}


HsailAgentStatus AgentDeleteFile(const char* ipFilename)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    if (ipFilename == nullptr)
    {
        AGENT_LOG("AgentDeleteFile: invalid filename");
        return status;
    }

    if (remove(ipFilename) != 0)
    {
        int err_no = errno;
        AGENT_ERROR("Error deleting " << ipFilename <<
                    ", errno: " << err_no << " " << strerror(err_no));
    }
    else
    {
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }

    return status;

}


HsailAgentStatus AgentLoadFileAsSharedObject(const std::string& ipFilename)
{
    void* hModule = nullptr;
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;

    dlerror(); // clear error status before processing
    hModule = dlopen(ipFilename.c_str(), RTLD_LAZY );
    char* dllstatus = dlerror();

    if (nullptr != hModule)
    {
        AGENT_OP("File: "  << ipFilename << " loaded as a shared library");
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }
    else
    {
        if (nullptr != dllstatus)
        {
            AGENT_ERROR("\"" <<ipFilename << "\"" << "Not Loaded (error: " << dllstatus << ")");
        }
        else
        {
            AGENT_ERROR(ipFilename  << "\t Not Loaded " << dllstatus );
        }
    }

    return status;
}

HsailAgentStatus AgentWriteBinaryToFile(const void*  pBinary, size_t binarySize, const char*  pFilename)
{
    HsailAgentStatus status = HSAIL_AGENT_STATUS_FAILURE;
    if (pBinary == nullptr)
    {
        AgentErrorLog("WriteBinaryToFile: Error Binary is null\n");
        return status;
    }

    if (binarySize <= 0)
    {
        AgentErrorLog("WriteBinaryToFile: Error Binary size is invalid\n");
        return status;
    }

    if (pFilename == nullptr)
    {
        AgentErrorLog("WriteBinaryToFile: Filename is nullptr\n");
        return status;
    }

    FILE* pFd = fopen(pFilename, "wb");

    if (pFd == nullptr)
    {
        AgentErrorLog("WriteBinaryToFile: Error opening file\n");
        assert(!"Error opening file\n");
        return status;
    }

    size_t retSize = fwrite(pBinary, sizeof(char), binarySize, pFd);

    fclose(pFd);

    if (retSize != binarySize)
    {
        AgentErrorLog("WriteBinaryToFile: Error writing to file\n");
        assert(!"WriteBinaryToFile: Error: fwrite failure.");
    }
    else
    {
        status = HSAIL_AGENT_STATUS_SUCCESS;
    }

    return status;
}

bool AgentWriteDLLPathToString(const std::string& dllName, std::string& msg)
{
    // Same as struct link_map in <link.h>
    typedef struct LinkMap
    {
        void* pAddr;    //Difference between the address in the ELF file and the addresses in memory.
        char* pPath;    // Absolute file name object was found in.
        void* pLd;      // Dynamic section of the shared object.
        struct LinkMap* pNext, *pPrev; // Chain of loaded objects.
    } LinkMap;

    bool ret = false;
    void* hModule = nullptr;
    char* status = nullptr;

    dlerror(); // clear error status before processing
    hModule = dlopen(dllName.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    status = dlerror();

    if (nullptr != hModule)
    {
        // Get the path
        LinkMap* pLm = reinterpret_cast<LinkMap*>(hModule);
        msg.assign(pLm->pPath);
        msg += "\t Loaded";

        dlclose(hModule);
        ret = true;
    }
    else
    {
        if (nullptr != status)
        {
            msg += dllName + "\t Not Loaded (error " + status + ")";
        }
        else
        {
            msg += dllName + "\t Not Loaded (can be expected)";
        }

        ret = false;
    }

    return ret;
}
bool AgentIsWaveInfoBufferValid(      HwDbgStatus           dbeStatus,
                                const uint32_t              nWaves,
                                const HwDbgWavefrontInfo*   pWaveInfo,
                                      bool&                 isBufferEmptyOut)
{
    // If any thing is wrong, return false
    bool retCode = false;
    // Check the DBE error
    if (dbeStatus != HWDBG_STATUS_SUCCESS)
    {
        AGENT_ERROR("IsWaveInfoBufferOPValid: Error in HwDbgGetActiveWaves API Call" << GetDBEStatusString(dbeStatus));
        retCode = false;
        return retCode;
    }

    // zero waves are possible, see the divergenttest (with bp hsail:63)
    if (nWaves == 0)
    {
        AGENT_LOG("IsWaveInfoBufferOPValid: No active waves found ");
        if (pWaveInfo != nullptr)
        {
            AGENT_LOG("IsWaveInfoBufferOPValid: pWaveInfo should be null");
        }

        isBufferEmptyOut = true;
        retCode = true;
        return retCode;
    }

    // The regular cases, check the wave count and the pointer
    if (nWaves > 0 )
    {
        if (pWaveInfo == nullptr)
        {
            AGENT_ERROR("IsWaveInfoBufferOPValid: WaveInfo buffer is nullptr, nWaves = "<< nWaves);
            retCode = false;
        }
        else
        {
            retCode = true;
        }
    }
    else
    {
        AGENT_ERROR("Invalid no of waves");
    }

    return retCode;
}

bool AgentIsWorkItemPresentInWave(const HwDbgDim3&          workGroup,
                                  const HwDbgDim3&          workItem,
                                  const HwDbgWavefrontInfo* pWaveInfo)
{
    if (pWaveInfo == nullptr)
    {
        AgentErrorLog("AgentIsWorkItemPresentInWave: Waveinfo buffer is nullptr\n");
        return false;
    }

    bool isWgFound = false;
    bool isWiFound = false;


    if (CompareHwDbgDim3(pWaveInfo->workGroupId, workGroup))
    {
        isWgFound = true;
    }

    if (isWgFound)
    {
        for (int i = 0; i < HWDBG_WAVEFRONT_SIZE; i++)
        {
            if (CompareHwDbgDim3(pWaveInfo->workItemId[i], workItem))
            {
                isWiFound = true;
                break;
            }
        }
    }

    return (isWgFound && isWiFound);
}



void ExtractSymbolListFromELFBinary(const void* pBinary,
                                    size_t binarySize,
                                    std::vector<std::pair<std::string, uint64_t>>& outputSymbols)
{
    if ((nullptr == pBinary) || (0 == binarySize))
    {
        return;
    }

    // Determine the ELF type:
    bool isELF = false;
    bool isELF32 = false;
    bool isELF64 = false;

    // The ELF executable header is 16 bytes:
    if (16 < binarySize)
    {
        // Check for the ELF header start:
        const unsigned char* pBinaryAsUBytes = (const unsigned char*)pBinary;
        isELF = ((0x7f == pBinaryAsUBytes[0]) &&
                 ('E'  == pBinaryAsUBytes[1]) &&
                 ('L'  == pBinaryAsUBytes[2]) &&
                 ('F'  == pBinaryAsUBytes[3]));
        isELF32 = isELF && (0x01 == pBinaryAsUBytes[4]);
        isELF64 = isELF && (0x02 == pBinaryAsUBytes[4]);
    }

    // Validate:
    if (!isELF)
    {
        return;
    }

    if (!isELF32 && !isELF64)
    {
        assert(!"Unsupported ELF sub-format!");
        return;
    }

    // Set the version of elf:
    elf_version(EV_CURRENT);

    // Initialize the binary as ELF from memory:
    Elf* pContainerElf = elf_memory((char*)pBinary, binarySize);

    if (nullptr == pContainerElf)
    {
        return;
    }

    // First get the symbol table section:
    const void* pSymTab = nullptr;
    size_t symTabSize = 0;
    int symStrTabIndex = -1;
    static const std::string symTabSectionName = ".symtab";

    // Get the shared strings section:
    size_t sectionHeaderStringSectionIndex = 0;
    int rcShrstr = elf_getshdrstrndx(pContainerElf, &sectionHeaderStringSectionIndex);

    if ((0 != rcShrstr) || (0 == sectionHeaderStringSectionIndex))
    {
        return;
    }

    // Iterate the sections to find the symbol table:
    Elf_Scn* pCurrentSection = elf_nextscn(pContainerElf, nullptr);

    while (nullptr != pCurrentSection)
    {
        size_t strOffset = 0;
        size_t shLink = 0;

        if (isELF32)
        {
            // Get the section header:
            Elf32_Shdr* pCurrentSectionHeader = elf32_getshdr(pCurrentSection);

            if (nullptr != pCurrentSectionHeader)
            {
                // Get the name and link:
                strOffset = pCurrentSectionHeader->sh_name;
                shLink = pCurrentSectionHeader->sh_link;
            }
        }
        else if (isELF64)
        {
            // Get the section header:
            Elf64_Shdr* pCurrentSectionHeader = elf64_getshdr(pCurrentSection);

            if (nullptr != pCurrentSectionHeader)
            {
                // Get the name and link:
                strOffset = pCurrentSectionHeader->sh_name;
                shLink = pCurrentSectionHeader->sh_link;
            }
        }

        // Get the current section's name:
        char* pCurrentSectionName = elf_strptr(pContainerElf, sectionHeaderStringSectionIndex, strOffset);

        if (nullptr != pCurrentSectionName)
        {
            if (symTabSectionName == pCurrentSectionName)
            {
                // Get the section's data:
                Elf_Data* pSectionData = elf_getdata(pCurrentSection, nullptr);

                if (nullptr != pSectionData)
                {
                    // We got the the section info:
                    pSymTab = pSectionData->d_buf;
                    symTabSize = (size_t)pSectionData->d_size;      /*NOTE: WHy is this 0*/

                    // The linked section is the symbol table string section:
                    symStrTabIndex = (int)shLink;

                    // Found the section, no need to continue:
                    break;
                }
            }
        }

        // Get the next section:
        pCurrentSection = elf_nextscn(pContainerElf, pCurrentSection);
    }

    if (nullptr == pSymTab || 0 == symTabSize || (0 >= symStrTabIndex))
    {
        return;
    }

    if (isELF32)
    {
        const int numberOfSymbols = (int)(symTabSize / sizeof(Elf32_Sym));
        Elf32_Sym* pCurrentSymbol = (Elf32_Sym*)pSymTab;

        for (int i = 0; numberOfSymbols > i; ++i, ++pCurrentSymbol)
        {
            // Get the symbol name as a string:
            char* pCurrentSymbolName = elf_strptr(pContainerElf, symStrTabIndex, pCurrentSymbol->st_name);

            if (nullptr != pCurrentSymbolName)
            {
                // Add the symbol name to the list:
                std::pair<std::string, uint64_t> symbol;
                symbol = std::make_pair(pCurrentSymbolName, pCurrentSymbol->st_value);

                outputSymbols.push_back(symbol);
            }
        }
    }
    else if (isELF64)
    {
        const int numberOfSymbols = (int)(symTabSize / sizeof(Elf64_Sym));
        Elf64_Sym* pCurrentSymbol = (Elf64_Sym*)pSymTab;

        for (int i = 0; numberOfSymbols > i; ++i, ++pCurrentSymbol)
        {
            // Get the symbol name as a string:
            char* pCurrentSymbolName = elf_strptr(pContainerElf, symStrTabIndex, pCurrentSymbol->st_name);

            if (nullptr != pCurrentSymbolName)
            {
                // Add the symbol name to the list:
                std::pair<std::string, uint64_t> symbol;
                symbol = std::make_pair(pCurrentSymbolName, pCurrentSymbol->st_value);

                outputSymbols.push_back(symbol);
            }
        }
    }
}

