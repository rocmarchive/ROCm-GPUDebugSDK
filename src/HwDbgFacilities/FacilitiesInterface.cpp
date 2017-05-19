//==============================================================================
// Copyright (c) 2014-2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Description: A C interface to HwDbgFacilities
//==============================================================================
// C / C++:
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>

// Brig:
#include "BrigSectionHeader.h"

// HwDbgFacilities:
#include "DbgInfoUtils.h"
#include "DbgInfoDwarfParser.h"
#include "DbgInfoConsumerImpl.h"
#include "DbgInfoCompoundConsumer.h"
#include "DbgInfoLogging.h"
#include "FacilitiesInterface.h"


#define HWDBGFAC_INTERFACE_DUMMY_FILE_PATH "src1.hsail"

// Shorthand to handle the optional pointer-to-output parameter:
#define HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, errcode) \
    { \
        if (nullptr != err) \
            *err = errcode; \
        return nullptr; \
    }

// Make sure that a pointed buffer is not zero-sized, and that a nullptr buffer is not given a size
#define HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(bufsize, buf) \
    if ((0 == bufsize && nullptr != buf) || (0 != bufsize && nullptr == buf)) \
        return HWDBGINFO_E_PARAMETER;

// Output a string and its length into a sized buffer / size output parameter combination
#define HWDBGFAC_INTERFACE_OUTPUT_STRING(str, outbuf, bufsize, strlen_out, err) \
    { \
        size_t l = str.length(); \
        if (nullptr != outbuf) \
        { \
            if (l >= bufsize) \
                err = HWDBGINFO_E_BUFFERTOOSMALL; \
            else \
            { \
                ::memcpy(outbuf, str.c_str(), l); \
                outbuf[l] = (char)0; \
            } \
        } \
        if (nullptr != strlen_out) \
            *strlen_out = l + 1; \
    }

// Validate that a sized array is large enough to contain the output
#define HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY(arrsize, outarr, outarrsize, err) \
    { \
        if (0 != outarrsize && arrsize > outarrsize) \
            err = HWDBGINFO_E_BUFFERTOOSMALL; \
    }

// Copy an array of a given type into an output buffer and its length into the size output parameter
// This macro assumes HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY was passed successfully.
#define HWDBGFAC_INTERFACE_OUTPUT_ARRAY(arr, arrtype, arrsize, outarr, outarrsize, arrsizeout) \
    { \
        if (nullptr != outarr) \
            ::memcpy(outarr, arr, arrsize * sizeof(arrtype)); \
        if (nullptr != arrsizeout) \
            *arrsizeout = arrsize; \
    }

// Return if an error is encountered
#define HWDBGFAC_INTERFACE_CHECKRETURN(err) if (HWDBGINFO_E_SUCCESS != err) return err;

// Get a C-style string from a pointer to an STL string, with the option of returning nullptr.
#define HWDBGFAC_INTERFACE_CSTR_FROM_STDSTRP(cstrvarname, stdstrpvarname) const char* cstrvarname = (nullptr != stdstrpvarname) ? (stdstrpvarname->c_str()) : nullptr

using namespace HwDbg;

// Shorthand typedefs:
typedef DbgInfoIConsumer<HwDbgUInt64, FileLocation, DwarfVariableLocation> DbgInfoConsumerInterface;
typedef DbgInfoConsumerImpl<HwDbgUInt64, FileLocation, DwarfVariableLocation> DbgInfoOneLevelConsumer;
typedef DbgInfoCompoundConsumer<HwDbgUInt64, FileLocation, DwarfVariableLocation, HwDbgUInt64, DwarfVariableLocation, FileLocation> DbgInfoTwoLevelConsumer;
typedef VariableInfo<HwDbgUInt64, DwarfVariableLocation> DbgInfoVariable;

// Helper structs:
struct HwDbgInfo_FacInt_Debug
{
    enum HwDbgInfo_FacInt_Debug_Type {
        HWDBGFAC_INTERFACE_ONE_LEVEL_DEBUG_INFO,
        HWDBGFAC_INTERFACE_TWO_LEVEL_DEBUG_INFO
    };

    HwDbgInfo_FacInt_Debug(HwDbgInfo_FacInt_Debug_Type tp) : m_tp(tp), m_cn(nullptr) { };
    virtual ~HwDbgInfo_FacInt_Debug()
    {
        size_t allocVarCount = m_allocatedVariableObjects.size();

        for (size_t i = 0; i < allocVarCount; i++)
        {
            delete m_allocatedVariableObjects[i];
        }

        m_allocatedVariableObjects.clear();
    }

    // Adds a variable to the allocated variables list
    void AddVariable(DbgInfoVariable* pVar)
    {
        // Try and place it in an empty spot:
        size_t allocVarCount = m_allocatedVariableObjects.size();

        for (size_t i = 0; i < allocVarCount; i++)
            if (nullptr == m_allocatedVariableObjects[i])
            {
                // Found a spot, place the variable here and exit:
                m_allocatedVariableObjects[i] = pVar;
                return;
            }

        // Could not find an empty spot, append the pointer to the vector:
        m_allocatedVariableObjects.push_back(pVar);
    }

    // Removes (all instances of) a variable from the allocated variables vector
    // and returns true if the variable was found.
    // DOES NOT DELETE THE VARIABLE OBJECT!
    bool RemoveVariable(DbgInfoVariable* pVar)
    {
        bool retVal = false;
        size_t allocVarCount = m_allocatedVariableObjects.size();

        // Look for the variable:
        for (size_t i = 0; i < allocVarCount; i++)
            if (m_allocatedVariableObjects[i] == pVar)
            {
                // Variable was found:
                retVal = true;
                m_allocatedVariableObjects[i] = nullptr;
            }

        return retVal;
    }

    // The debug info type
    const HwDbgInfo_FacInt_Debug_Type m_tp;

    // The "default" file name - or main CU file name. It is the first file mapped in the HL line table:
    std::string m_firstMappedFileName;

    // A vector of the variable objects allocated by the C API:
    std::vector<DbgInfoVariable*> m_allocatedVariableObjects;

    // The HSAIL source found inside the binary, if any:
    std::string m_hsailSource;

    // The consumer interface. Note that this class is not the owner of the consumer,
    // and memory management should be handled by derived classes:
    DbgInfoConsumerInterface* m_cn;
};

struct HwDbgInfo_FacInt_OneLevelDebug : public HwDbgInfo_FacInt_Debug
{
public:
    // Ctor
    HwDbgInfo_FacInt_OneLevelDebug() : HwDbgInfo_FacInt_Debug(HWDBGFAC_INTERFACE_ONE_LEVEL_DEBUG_INFO), ol_cn(nullptr) {};

    // Dtor
    virtual ~HwDbgInfo_FacInt_OneLevelDebug()
    {
        delete ol_cn;
        ol_cn = nullptr;
    };

    // One-level debug information
    DbgInfoDwarfParser::DwarfCodeScope ol_sc;   // One-level variable debug info
    DbgInfoDwarfParser::DwarfLineMapping ol_lm; // One-level line debug info
    DbgInfoOneLevelConsumer* ol_cn;             // One-level debug info consumer
};

struct HwDbgInfo_FacInt_TwoLevelDebug : public HwDbgInfo_FacInt_Debug
{
public:
    // Ctor
    HwDbgInfo_FacInt_TwoLevelDebug() :
        HwDbgInfo_FacInt_Debug(HWDBGFAC_INTERFACE_TWO_LEVEL_DEBUG_INFO),
        hl_cn(nullptr), ll_cn(nullptr), ol_cn_owned(false), tl_cn(nullptr), llFileName(HWDBGFAC_INTERFACE_DUMMY_FILE_PATH), brig_code(nullptr, 0), brig_strtab(nullptr, 0) {};

    // Dtor
    virtual ~HwDbgInfo_FacInt_TwoLevelDebug()
    {
        delete tl_cn; tl_cn = nullptr; m_cn = nullptr;

        if (ol_cn_owned)
        {
            delete hl_cn; delete ll_cn;
        }

        hl_cn = nullptr; ll_cn = nullptr;
    };

    // High-level debug information
    DbgInfoDwarfParser::DwarfCodeScope hl_sc;   // High-level variable debug info
    DbgInfoDwarfParser::DwarfLineMapping hl_lm; // High-level line debug info
    DbgInfoOneLevelConsumer* hl_cn;             // High-level debug info consumer

    // Low-level debug information
    DbgInfoDwarfParser::DwarfCodeScope ll_sc;   // Low-level variable debug info
    DbgInfoDwarfParser::DwarfLineMapping ll_lm; // Low-level line debug info
    DbgInfoOneLevelConsumer* ll_cn;             // Low-level debug info consumer

    // Ownership of the one-level consumers:
    bool ol_cn_owned;

    // Two-level debug information consumer, combining the two levels
    DbgInfoTwoLevelConsumer* tl_cn;

    // The file name used for the "source locations" in the low-level debug information
    const std::string llFileName;

    // Pointers to the BRIG code and string table sections
    KernelBinary brig_code;
    KernelBinary brig_strtab;
};

// Helper functions:

// HL address to LL line resolver for the two-level debug information consumer:
FileLocation HwDbgInfoAddressResolver(const HwDbgUInt64& hlAddr, void* dbg)
{
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;
    HwDbgInfo_FacInt_TwoLevelDebug* pTLDbg = static_cast<HwDbgInfo_FacInt_TwoLevelDebug*>(pDbg);

    if (nullptr != pDbg)
    {
        FileLocation llLine(pTLDbg->llFileName, hlAddr);
        return llLine;
    }

    FileLocation llLine(HWDBGFAC_INTERFACE_DUMMY_FILE_PATH, hlAddr);
    return llLine;
}

// LL line to HL address resolver for the two-level debug information consumer:
HwDbgUInt64 HwDbgInfoLineResolver(const FileLocation& llLine, void* dbg)
{
    (void)dbg;
    HwDbgUInt64 hAddr = llLine.m_lineNum;
    return hAddr;
}

// Matching function for variable by BRIG offset:
bool MatchByBrigOffset(const DbgInfoTwoLevelConsumer::LowLvlVariableInfo& var, const void* matchData, const DbgInfoTwoLevelConsumer::LowLvlVariableInfo*& pFoundMember)
{
    // [US] 14/5/15 - Currently, we do not handle members of HLL variables. If we want to support such code, we would need to attach the member names to the
    // match data to get them in the future.
    bool retVal = (var.m_brigOffset == (*(unsigned int*)matchData));

    // Output the found "member":
    if (retVal)
    {
        pFoundMember = &var;
    }

    return retVal;
}

// Varibale location resolver for the two-level debug information consumer:
bool HwDbgInfoLocationResolver(const HwDbg::DwarfVariableLocation& hVarLoc, const HwDbgUInt64& lAddr, const HwDbg::DbgInfoIConsumer<HwDbgUInt64, HwDbg::FileLocation, HwDbg::DwarfVariableLocation>& lConsumer, HwDbg::DwarfVariableLocation& o_lVarLocation, void* dbg)
{
    (void)dbg;

    // Validate the high location is an "address":
    if (hVarLoc.m_locationRegister != HwDbg::DwarfVariableLocation::LOC_REG_NONE)
    {
        return false;
    }

    // The target's m_brigOffset should match the HL variable's m_locationOffset:
    DbgInfoVariable tempLocation;
    tempLocation.m_varValue.m_varValueLocation.Initialize();

    // Find the variable information using the low level consumer using the variable brig offset:
    bool rc = lConsumer.GetMatchingVariableInfoInCurrentScope(lAddr, MatchByBrigOffset, &hVarLoc.m_locationOffset, tempLocation);

    // Validations:
    // Offset should be identical:
    if (hVarLoc.m_locationOffset != tempLocation.m_brigOffset)
    {
        return false;
    }

    if (!rc)
    {
        return false;
    }

    // Get the location from the variable information:
    o_lVarLocation.Initialize();
    o_lVarLocation = tempLocation.m_varValue.m_varValueLocation;
    o_lVarLocation.m_locationOffset += hVarLoc.m_constAddition;

    // Copy fields that are kept (resource and piece information):
    if (UINT64_MAX == o_lVarLocation.m_locationResource)
    {
        // Copy the resource if it wasn't supplied in the LL info:
        o_lVarLocation.m_locationResource = hVarLoc.m_locationResource;
    }

    if (UINT32_MAX == o_lVarLocation.m_isaMemoryRegion)
    {
        // Copy the ISA region if it wasn't supplied in the LL info:
        o_lVarLocation.m_isaMemoryRegion = hVarLoc.m_isaMemoryRegion;
    }

    o_lVarLocation.m_pieceOffset += hVarLoc.m_pieceOffset;

    if (o_lVarLocation.m_pieceSize > hVarLoc.m_pieceSize)
    {
        o_lVarLocation.m_pieceSize = hVarLoc.m_pieceSize;
    }

    return true;
}

//////////////////////////////////////////////////////////////////////////
// C API functions                                                      //
//////////////////////////////////////////////////////////////////////////

// Initialize a HwDbgInfo_debug from a single- or two- level binary
HwDbgInfo_debug hwdbginfo_init_and_identify_binary(const void* bin, size_t bin_size, HwDbgInfo_err* err)
{
    // Validate input:
    if (nullptr == bin || 0 == bin_size)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOBINARY);
    }

    // Look for the high-level BRIG and binary info:
    HwDbgInfo_debug dbg = hwdbginfo_init_with_hsa_1_0_binary(bin, bin_size, err);
    if (nullptr != dbg)
    {
        return dbg;
    }

    // Two-level information not found. Attempt to initialize as one-level binary:
    dbg = hwdbginfo_init_with_single_level_binary(bin, bin_size, err);

    return dbg;
}

// Initialize a HwDbgInfo_debug from a single level ELF/DWARF binary:
HwDbgInfo_debug hwdbginfo_init_with_single_level_binary(const void* bin, size_t bin_size, HwDbgInfo_err* err)
{

    // Validate input:
    if (nullptr == bin || 0 == bin_size)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOBINARY);
    }

    char* loggingOption = getenv("HWDBG_DBGINFO_ENABLE_LOGGING");
    if (loggingOption != nullptr)
    {
        hwdbginfo_enable_logging();
    }
    // Create the binary object:
    KernelBinary olBin(bin, bin_size);

    // Create the output struct:
    HwDbgInfo_FacInt_OneLevelDebug* dbg = new(std::nothrow) HwDbgInfo_FacInt_OneLevelDebug;

    if (nullptr == dbg)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_OUTOFMEMORY);
    }

    // Parse:
    bool retVal = DbgInfoDwarfParser::InitializeWithBinary(olBin, dbg->ol_sc, dbg->ol_lm);

    // for each scope
    for(size_t i=0; i < dbg->ol_sc.m_children.size(); i++)
    {
        DBGINFO_LOG( "===========Scope # " << i << "============\n");
        // For each address range within the scope
        for (size_t k=0; k< dbg->ol_sc.m_children[i]->m_scopeAddressRanges.size(); k++)
        {
            DBGINFO_LOG( "\t"
                      << std::hex
                      << "Low PC: 0x" << dbg->ol_sc.m_children[i]->m_scopeAddressRanges[k].m_minAddr << "\t"
                      << "High PC: 0x" << dbg->ol_sc.m_children[i]->m_scopeAddressRanges[k].m_maxAddr << "\t"
                      << "Name: \"" << dbg->ol_sc.m_children[i]->m_scopeName << "\""
                      << std::dec
                      << "\n");
        }

        // for all vars in that scope
        for (size_t j=0;j< dbg->ol_sc.m_children[i]->m_scopeVars.size(); j++)
        {
            if (dbg->ol_sc.m_children[i]->m_scopeVars[j]->m_varName.empty())
            {
                DBGINFO_LOG("EMPTY Name: \t");
            }
            else
            {
                DBGINFO_LOG("Var Name: \"" << dbg->ol_sc.m_children[i]->m_scopeVars[j]->m_varName << "\"\t");
            }

            DBGINFO_LOG( "Type Name: \"" << dbg->ol_sc.m_children[i]->m_scopeVars[j]->m_typeName << "\"\t"
                        << std::hex
                        << "LowPC: 0x" << dbg->ol_sc.m_children[i]->m_scopeVars[j]->m_lowVariablePC << "\t"
                        << "HighPC: 0x" << dbg->ol_sc.m_children[i]->m_scopeVars[j]->m_highVariablePC << "\n"
                        << std::dec);
        }
    }

    if (!retVal)
    {
        delete dbg;
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_HLINFO);
    }

    // Initialize consumer:
    dbg->ol_cn = new(std::nothrow) DbgInfoOneLevelConsumer;

    if (nullptr == dbg->ol_cn)
    {
        delete dbg;
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_OUTOFMEMORY);
    }

    // Set the parent struct's value:
    dbg->m_cn = dbg->ol_cn;

    dbg->ol_cn->SetCodeScope(&dbg->ol_sc);
    dbg->ol_cn->SetLineNumberMap(&dbg->ol_lm);

    // Set the default file name:
    std::vector<FileLocation> ol_fileLocs;
    bool rcHLLM = dbg->ol_lm.GetMappedLines(ol_fileLocs);

    DBGINFO_LOG("Print all File names\n");
    for (size_t i = 0 ; i< ol_fileLocs.size(); i++)
      {
        DBGINFO_LOG("Mapping  " << i << "Path: " << ol_fileLocs[i].fullPath() <<
                               "Line " << ol_fileLocs[i].m_lineNum  <<  "\n");
      }

    if (rcHLLM)
    {
        size_t fileLocCount = ol_fileLocs.size();

        for (size_t i = 0; i < fileLocCount; i++)
        {
            std::string currentFileName = ol_fileLocs[i].fullPath();

            if (!currentFileName.empty())
            {
                dbg->m_firstMappedFileName = currentFileName;
                break;
            }
        }
    }

    // Report success:
    if (nullptr != err)
    {
        *err = HWDBGINFO_E_SUCCESS;
    }

    HwDbgInfo_FacInt_Debug* pBaseDbg = static_cast<HwDbgInfo_FacInt_Debug*>(dbg);

    return (HwDbgInfo_debug)pBaseDbg;
}

// Initialize a HwDbgInfo_debug from an HSA 1.0 (May 2015 design) binary:
HwDbgInfo_debug hwdbginfo_init_with_hsa_1_0_binary(const void* bin, size_t bin_size, HwDbgInfo_err* err)
{
    // Validate input:
    if (nullptr == bin || 0 == bin_size)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOBINARY);
    }

    // Create the binary object:
    KernelBinary hsa10Bin(bin, bin_size);

    // The HL DWARF is inside the BRIG Code object, which is section .hsa.brig:
    static const std::string brigCodeObjectSectionNamePrefix = ".hsahldebug_";
    static const size_t brigCodeObjectSectionNamePrefixLen = brigCodeObjectSectionNamePrefix.length();
    std::string brigCodeObjectSectionName;
    std::vector<std::string> hsa10BinSections;
    hsa10Bin.listELFSectionNames(hsa10BinSections);
    size_t secCount = hsa10BinSections.size();

    for (size_t i = 0; i < secCount; ++i)
    {
        // See if the prefix is valid:
        const std::string& currSec = hsa10BinSections[i];

        if (0 == currSec.compare(0, brigCodeObjectSectionNamePrefixLen, brigCodeObjectSectionNamePrefix))
        {
            // Found a code object!
            brigCodeObjectSectionName = currSec;
            // (Multiple code objects are not currently supported)
            break;
        }
    }

    // If no code object was found:
    if (brigCodeObjectSectionName.empty())
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOHLBINARY);
    }

    KernelBinary brigCodeObject(nullptr, 0);
    bool retVal = hsa10Bin.getElfSectionAsBinary(brigCodeObjectSectionName, brigCodeObject);

    if (!retVal)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOHLBINARY);
    }

    // In the HSA 1.0 spec, the debug information is saved under the code object:
    KernelBinary& hlBin = brigCodeObject;

    // Get the HSAIL text, if available:
    KernelBinary hsailText(nullptr, 0);
    KernelBinary hsailTextBrigSection(nullptr, 0);
    static const std::string hsailTextSectionName = ".source";
    bool rcText = brigCodeObject.getElfSectionAsBinary(hsailTextSectionName, hsailTextBrigSection);

    if (rcText && hsailTextBrigSection.m_pBinaryData && sizeof(BrigSectionHeader) < hsailTextBrigSection.m_binarySize)
    {
        // If this has a BRIG section header:
        const BrigSectionHeader* pHsailTextSecHdr = (const BrigSectionHeader*)hsailTextBrigSection.m_pBinaryData;

        if (((uint64_t)pHsailTextSecHdr->headerByteCount < pHsailTextSecHdr->byteCount) &&
            (pHsailTextSecHdr->headerByteCount > 0) &&
            (pHsailTextSecHdr->byteCount <= (uint64_t)hsailTextBrigSection.m_binarySize))
        {
            // Skip the BRIG section header:
            rcText = hsailTextBrigSection.getTrimmedBufferAsBinary((size_t)pHsailTextSecHdr->headerByteCount, 0, hsailText);
        }
        else
        {
#ifdef HWDBGINFO_MOVE_SEMANTICS
            // Move the data:
            hsailText = static_cast < KernelBinary && >(hsailTextBrigSection);
#else
            // Copy the data:
            hsailText = hsailTextBrigSection;
#endif
        }
    }

    // The LL DWARF is in section .debug_.sc_elf:
    static const std::string llSectionName = ".debug_.sc_elf";
    KernelBinary llBin(nullptr, 0);
    retVal = hsa10Bin.getElfSectionAsBinary(llSectionName, llBin);

    if (!retVal)
    {
        // The final HSA 1.0 format has the DWARF information in the main HSA file. If it's there, use it now:
        static const std::string debugInfoSecionName1 = ".debug_info";
        static const std::string debugInfoSecionName2 = ".debug_line";
        bool foundSection1 = false;
        bool foundSection2 = false;

        for (size_t i = 0; i < secCount; ++i)
        {
            // Search for the debug sections:
            const std::string& currSec = hsa10BinSections[i];

            if (debugInfoSecionName1 == currSec)
            {
                foundSection1 = true;
            }
            else if (debugInfoSecionName2 == currSec)
            {
                foundSection2 = true;
            }
        }

        // If both debug sections are present (no need to pass them on, just check they are there:
        if (foundSection1 && foundSection2)
        {
            // Copy the entire buffer, since it is the debug info container:
            hsa10Bin.getSubBufferAsBinary(0, hsa10Bin.m_binarySize, llBin);
        }
        else
        {
            // LL debug info not found:
            HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOLLBINARY);
        }
    }

    // Initialize with the sub-binaries and BRIG data:
    HwDbgInfo_debug dbg = hwdbginfo_init_with_two_binaries(hlBin.m_pBinaryData, hlBin.m_binarySize, llBin.m_pBinaryData, llBin.m_binarySize, err);

    // If this succeeded:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr != pDbg)
    {
        /*
        // [US] 13/5/15 - the .hsatext section is assumed to be the BRIG code, as it is
        // binary data. If there are problems in the future with the code object, look into this.
        // If the HSAIL text was not in the code object:
        if ((nullptr != hsailText.m_pBinaryData) && (0 < hsailText.m_binarySize))
        {
            // Try to get the HSAIL text from the HSA text section:
            static const std::string hsailTextSectionName = ".hsatext";
            hsa10Bin.getElfSectionAsBinary(hsailTextSectionName);
        }
        */

        if ((nullptr != hsailText.m_pBinaryData) && (0 < hsailText.m_binarySize))
        {
            // Save it for access:
            pDbg->m_hsailSource.assign((char*)hsailText.m_pBinaryData, hsailText.m_binarySize);
        }
    }

    return dbg;
}

// Initialize a HwDbgInfo_debug from two binaries containing debug information(without pointers to the BRIG information):
HwDbgInfo_debug hwdbginfo_init_with_two_binaries(const void* hl_bin, size_t hl_bin_size, const void* ll_bin, size_t ll_bin_size, HwDbgInfo_err* err)
{
    // Parameter validation:
    if (nullptr == hl_bin || 0 == hl_bin_size)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOHLBINARY);
    }

    if (nullptr == ll_bin || 0 == ll_bin_size)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOLLBINARY);
    }

    KernelBinary hlBin(hl_bin, hl_bin_size);
    KernelBinary llBin(ll_bin, ll_bin_size);

    // Create the output struct:
    HwDbgInfo_FacInt_TwoLevelDebug* dbg = new(std::nothrow) HwDbgInfo_FacInt_TwoLevelDebug;

    if (nullptr == dbg)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_OUTOFMEMORY);
    }

    // Parse:
    bool retVal = DbgInfoDwarfParser::InitializeWithBinary(hlBin, dbg->hl_sc, dbg->hl_lm);

    if (!retVal)
    {
        delete dbg;
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_HLINFO);
    }

    retVal = DbgInfoDwarfParser::InitializeWithBinary(llBin, dbg->ll_sc, dbg->ll_lm, dbg->llFileName);

    if (!retVal)
    {
        delete dbg;
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_LLINFO);
    }

    // Initialize consumers:
    dbg->hl_cn = new(std::nothrow) DbgInfoOneLevelConsumer;
    dbg->ll_cn = new(std::nothrow) DbgInfoOneLevelConsumer;
    dbg->ol_cn_owned = true;

    if (nullptr == dbg->hl_cn || nullptr == dbg->ll_cn)
    {
        delete dbg;
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_OUTOFMEMORY);
    }

    dbg->hl_cn->SetCodeScope(&dbg->hl_sc);
    dbg->hl_cn->SetLineNumberMap(&dbg->hl_lm);
    dbg->ll_cn->SetCodeScope(&dbg->ll_sc);
    dbg->ll_cn->SetLineNumberMap(&dbg->ll_lm);

    dbg->tl_cn = new(std::nothrow) DbgInfoTwoLevelConsumer(dbg->hl_cn, dbg->ll_cn, HwDbgInfoLocationResolver, HwDbgInfoAddressResolver, HwDbgInfoLineResolver, (void*)dbg);

    if (nullptr == dbg->tl_cn)
    {
        delete dbg;
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_OUTOFMEMORY);
    }

    // Set the parent struct's value:
    dbg->m_cn = dbg->tl_cn;

    // Transfer ownership of the one-level consumers to the two-level consumer:
    dbg->ol_cn_owned = false;

    // Set the default file name:
    std::vector<FileLocation> hl_fileLocs;
    bool rcHLLM = dbg->hl_lm.GetMappedLines(hl_fileLocs);

    if (rcHLLM)
    {
        size_t fileLocCount = hl_fileLocs.size();

        for (size_t i = 0; i < fileLocCount; i++)
        {
            std::string currentFileName = hl_fileLocs[i].fullPath();

            if (!currentFileName.empty())
            {
                dbg->m_firstMappedFileName = currentFileName;
                break;
            }
        }
    }

    // Report success:
    if (nullptr != err)
    {
        *err = HWDBGINFO_E_SUCCESS;
    }

    HwDbgInfo_FacInt_Debug* pBaseDbg = static_cast<HwDbgInfo_FacInt_Debug*>(dbg);

    return (HwDbgInfo_debug)pBaseDbg;
}

// Get the HSAIL source from the binary:
HwDbgInfo_err hwdbginfo_get_hsail_text(HwDbgInfo_debug dbg, const char** hsail_source, size_t* hsail_source_len)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || nullptr == hsail_source)
    {
        return HWDBGINFO_E_PARAMETER;
    }

    if (pDbg->m_hsailSource.empty())
    {
        return HWDBGINFO_E_NOSOURCE;
    }

    *hsail_source = pDbg->m_hsailSource.c_str();

    if (nullptr != hsail_source_len)
    {
        *hsail_source_len = pDbg->m_hsailSource.length() + 1;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Create a HwDbgInfo_code_location:
HwDbgInfo_code_location hwdbginfo_make_code_location(const char* file_name, HwDbgInfo_linenum line_num)
{
    std::string strPath;

    if (nullptr != file_name)
    {
        strPath.assign(file_name);
    }

    FileLocation* pLoc = new(std::nothrow) FileLocation(strPath, line_num);

    return (HwDbgInfo_code_location)pLoc;
}

// Query a HwDbgInfo_code_location:
HwDbgInfo_err hwdbginfo_code_location_details(HwDbgInfo_code_location loc, HwDbgInfo_linenum* line_num, size_t buf_len, char* file_name, size_t* file_name_len)
{
    // Parameter validation:
    FileLocation* pLoc = (FileLocation*)loc;

    if ((nullptr == pLoc) || (nullptr == line_num && 0 == buf_len && nullptr == file_name && nullptr == file_name_len))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(buf_len, file_name);

    // Output file path:
    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;

    const char* pFullPath = pLoc->fullPath();
    if (nullptr == pFullPath)
    {
        static const std::string emptyStr;
        HWDBGFAC_INTERFACE_OUTPUT_STRING(emptyStr, file_name, buf_len, file_name_len, err);
    }
    else
    {
        std::string fullPath = pFullPath;
        HWDBGFAC_INTERFACE_OUTPUT_STRING(fullPath, file_name, buf_len, file_name_len, err);
    }

    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    // Output line number:
    if (nullptr != line_num)
    {
        *line_num = pLoc->m_lineNum;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Query a HwDbgInfo_frame_context
HwDbgInfo_err hwdbginfo_frame_context_details(HwDbgInfo_frame_context frm, HwDbgInfo_addr* pc, HwDbgInfo_addr* fp, HwDbgInfo_addr* mp, HwDbgInfo_code_location* loc, size_t buf_len, char* func_name, size_t* func_name_len)
{
    // Parameter validation:
    DbgInfoTwoLevelConsumer::TwoLvlCallStackFrame* pFrame = (DbgInfoTwoLevelConsumer::TwoLvlCallStackFrame*)frm;

    if ((nullptr == frm) || (nullptr == pc && nullptr == fp && nullptr == mp && nullptr == loc && 0 == buf_len && nullptr == func_name && nullptr == func_name_len))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(func_name, func_name_len);

    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;

    // Output code location:
    if (nullptr != loc)
    {
        *loc = hwdbginfo_make_code_location(pFrame->m_sourceLocation.fullPath(), pFrame->m_sourceLocation.m_lineNum);
    }

    if (nullptr == *loc)
    {
        return HWDBGINFO_E_OUTOFMEMORY;
    }

    // Output the function name:
    HWDBGFAC_INTERFACE_OUTPUT_STRING(pFrame->m_functionName, func_name, buf_len, func_name_len, err);
    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    // Output program counter, frame pointer, and module pointer:
    if (nullptr != pc)
    {
        *pc = pFrame->m_programCounter;
    }

    if (nullptr != fp)
    {
        *fp = pFrame->m_functionBase;
    }

    if (nullptr != mp)
    {
        *mp = pFrame->m_moduleBase;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Matches a line number from an address:
HwDbgInfo_err hwdbginfo_addr_to_line(HwDbgInfo_debug dbg, HwDbgInfo_addr addr, HwDbgInfo_code_location* loc)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || nullptr == loc)
    {
        return HWDBGINFO_E_PARAMETER;
    }

    // Query the debug info:
    FileLocation matchedLine;
    bool rc = pDbg->m_cn->GetLineFromAddress(addr, matchedLine);

    if (!rc)
    {
        return HWDBGINFO_E_NOTFOUND;
    }

    // Output the location:
    *loc = hwdbginfo_make_code_location(matchedLine.fullPath(), matchedLine.m_lineNum);

    if (nullptr == *loc)
    {
        return HWDBGINFO_E_OUTOFMEMORY;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Get the list of addresses that matches a line number
HwDbgInfo_err hwdbginfo_line_to_addrs(HwDbgInfo_debug dbg, HwDbgInfo_code_location loc, size_t buf_len, HwDbgInfo_addr* addrs, size_t* addr_count)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;
    FileLocation* pLoc = (FileLocation*)loc;

    if (nullptr == pDbg || nullptr == pLoc || (0 == buf_len && nullptr == addrs && nullptr == addr_count))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(buf_len, addrs);

    // Query the debug info:
    std::vector<DwarfAddrType> matchedAddrs;
    bool rc = pDbg->m_cn->GetAddressesFromLine(*pLoc, matchedAddrs, true, false); // All HL addresses, first LL address for each one

    if (!rc)
    {
        if (nullptr != addr_count)
        {
            *addr_count = 0;
        }

        return HWDBGINFO_E_NOTFOUND;
    }

    // Output the addresses:
    size_t matchAddrCount = matchedAddrs.size();

    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;
    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY(matchAddrCount, addrs, buf_len, err);
    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    void* matchAddrBuf = (void*)(&(matchedAddrs[0]));
    HWDBGFAC_INTERFACE_OUTPUT_ARRAY(matchAddrBuf, DwarfAddrType, matchAddrCount, addrs, buf_len, addr_count);

    return HWDBGINFO_E_SUCCESS;
}

// Finds the closest valid line number to an input line number:
HwDbgInfo_err hwdbginfo_nearest_mapped_line(HwDbgInfo_debug dbg, HwDbgInfo_code_location base_line, HwDbgInfo_code_location* line)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;
    FileLocation* pBaseLine = (FileLocation*)base_line;

    if (nullptr == pDbg || nullptr == pBaseLine || nullptr == line)
    {
        return HWDBGINFO_E_PARAMETER;
    }

    // If the mapped line does not have a file name:
    bool wasEmptyPath = false;

    const char* pBasePath = pBaseLine->fullPath();
    // Case 1: If no input file name given the in the input code location
    if (nullptr == pBasePath || '\0' == pBasePath[0])
    {
        wasEmptyPath = true;

        // Get the first file name:
        if (!pDbg->m_firstMappedFileName.empty())
        {
            pBaseLine->setFullPath(pDbg->m_firstMappedFileName);
        }
    }
    else
    {
        // Case 2: Some input file name given.
        //
        // Handle the case when a filename is provided while creating the code location.
        // We need to look for a full path for the filename by comparing all the file names
        // we can find the line table

        std::vector<FileLocation> ol_fileLocs;

        // Cast the generic base into the One level specific structure.
        // A better design would be to add a GetMappedLines() to the consumer interface class
        HwDbgInfo_FacInt_OneLevelDebug* ol_dbg  = (HwDbgInfo_FacInt_OneLevelDebug*)(pDbg);
        assert(nullptr != ol_dbg);

        bool rcHLLM = ol_dbg->ol_lm.GetMappedLines(ol_fileLocs);
        assert( rcHLLM == true);
        if (rcHLLM == true)
        {
            for (size_t i=0;i < ol_fileLocs.size() ; i++)
            {
                // skip null
                if (ol_fileLocs[i].m_fullPath == nullptr)
                {
                    continue;
                }

                // Get just the filename from the DWARF full path
                char* fileName  = basename(ol_fileLocs[i].m_fullPath);

                // Compare input filename and the DWARF file name
                if (fileName != NULL)
                {
                    // If the filename matches, we are good.
                    // This can be improved later to handle relative paths
                    if (strcmp(fileName, pBasePath) == 0)
                    {
                        pBaseLine->setFullPath(ol_fileLocs[i].m_fullPath);
                        break;
                    }
                }
            }
        }
        else
        {
            DBGINFO_LOG("Could not find any mapped lines");
        }
    }

    // Query the debug info:
    FileLocation matchedLine;
    bool rc = pDbg->m_cn->GetNearestMappedLine(*pBaseLine, matchedLine);

    // Restore the value before even checking for validity:
    if (wasEmptyPath)
    {
        pBaseLine->clearFullPath();
    }

    if (!rc)
    {
        return HWDBGINFO_E_NOTFOUND;
    }

    // Output the line number:
    *line = hwdbginfo_make_code_location(matchedLine.fullPath(), matchedLine.m_lineNum);

    if (nullptr == *line)
    {
        return HWDBGINFO_E_OUTOFMEMORY;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Finds the closest valid address to an input address:
HwDbgInfo_err hwdbginfo_nearest_mapped_addr(HwDbgInfo_debug dbg, HwDbgInfo_addr base_addr, HwDbgInfo_addr* addr)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || nullptr == addr)
    {
        return HWDBGINFO_E_PARAMETER;
    }

    // Query the debug info:
    HwDbgUInt64 matchedAddr = 0;
    bool rc = pDbg->m_cn->GetNearestMappedAddress(base_addr, matchedAddr);

    if (!rc)
    {
        return HWDBGINFO_E_NOTFOUND;
    }

    // Output the address:
    *addr = (HwDbgInfo_addr)matchedAddr;

    return HWDBGINFO_E_SUCCESS;
}

// Gets first legal (mapped) HL filepath
HwDbgInfo_err hwdbginfo_first_file_name(HwDbgInfo_debug dbg, size_t buf_len, char* file_name, size_t* file_name_len)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || (0 == buf_len && nullptr == file_name && nullptr == file_name_len))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(buf_len, file_name);

    // Output file path:
    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;

    const std::string& fullPath = pDbg->m_firstMappedFileName;

    // If the first path is empty, there are no file paths in the debug info:
    if (fullPath.empty())
    {
        return HWDBGINFO_E_NOTFOUND;
    }

    // Copy the value:
    HWDBGFAC_INTERFACE_OUTPUT_STRING(fullPath, file_name, buf_len, file_name_len, err);

    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    return HWDBGINFO_E_SUCCESS;

}

// Gets a list of all mapped addresses, for a "step into" operation:
HwDbgInfo_err hwdbginfo_all_mapped_addrs(HwDbgInfo_debug dbg, size_t buf_len, HwDbgInfo_addr* addrs, size_t* addr_count)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || (0 == buf_len && nullptr == addrs && nullptr == addr_count))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(buf_len, addrs);

    // Query the debug info:
    std::vector<DwarfAddrType> mappedAddrs;
    bool rc = pDbg->m_cn->GetMappedAddresses(mappedAddrs);

    if (!rc)
    {
        if (nullptr != addr_count)
        {
            *addr_count = 0;
        }

        return HWDBGINFO_E_NOTFOUND;
    }

    // Output the addresses:
    size_t mappedAddrCount = mappedAddrs.size();

    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;
    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY(mappedAddrCount, addrs, buf_len, err);
    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    void* mappedAddrBuf = (void*)(&(mappedAddrs[0]));
    HWDBGFAC_INTERFACE_OUTPUT_ARRAY(mappedAddrBuf, DwarfAddrType, mappedAddrCount, addrs, buf_len, addr_count);

    return HWDBGINFO_E_SUCCESS;
}

// Gets an address's virtual call stack (of inlined functions):
HwDbgInfo_err hwdbginfo_addr_call_stack(HwDbgInfo_debug dbg, HwDbgInfo_addr start_addr, size_t buf_len, HwDbgInfo_frame_context* stack_frames, size_t* frame_count)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || (0 == buf_len && nullptr == stack_frames && nullptr == frame_count))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(buf_len, stack_frames);

    // Query the debug info:
    std::vector<DbgInfoTwoLevelConsumer::TwoLvlCallStackFrame> cs;
    bool rc = pDbg->m_cn->GetAddressVirtualCallStack(start_addr, cs);

    if (!rc)
    {
        if (nullptr != frame_count)
        {
            *frame_count = 0;
        }

        return HWDBGINFO_E_NOTFOUND;
    }

    // Output the stack frames:
    size_t frameCount = cs.size();

    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;
    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY(frameCount, stack_frames, buf_len, err);
    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    if (nullptr != stack_frames)
    {
        for (size_t i = 0; i < buf_len; i++)
        {
            // Copy each valid frame to the output buffer:
            if (i < frameCount)
            {
                // Allocated a new frame:
                const DbgInfoTwoLevelConsumer::TwoLvlCallStackFrame& currentFrame = cs[i];
                DbgInfoTwoLevelConsumer::TwoLvlCallStackFrame* pNewFrame = new(std::nothrow) DbgInfoTwoLevelConsumer::TwoLvlCallStackFrame;

                if (nullptr == pNewFrame)
                {
                    // Allocation failed, delete all previous items:
                    hwdbginfo_release_frame_contexts(stack_frames, i);
                    return HWDBGINFO_E_OUTOFMEMORY;
                }

                // Copy frame data:
                pNewFrame->m_programCounter = currentFrame.m_programCounter;
                pNewFrame->m_functionBase = currentFrame.m_functionBase;
                pNewFrame->m_moduleBase = currentFrame.m_moduleBase;
                pNewFrame->m_sourceLocation = currentFrame.m_sourceLocation;
                pNewFrame->m_functionName = currentFrame.m_functionName;

                stack_frames[i] = (HwDbgInfo_frame_context)pNewFrame;
            }
            else
            {
                stack_frames[i] = nullptr;
            }
        }
    }

    // Output the stack size:
    if (nullptr != frame_count)
    {
        *frame_count = frameCount;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Returns a list of step (over or out) target addresses from a base address:
HwDbgInfo_err hwdbginfo_step_addresses(HwDbgInfo_debug dbg, HwDbgInfo_addr start_addr, bool step_out, size_t buf_len, HwDbgInfo_addr* addrs, size_t* addr_count)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || (0 == buf_len && nullptr == addrs && nullptr == addr_count))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(buf_len, addrs);

    // Query the debug info:
    std::vector<DwarfAddrType> stepAddrs;
    bool rc = pDbg->m_cn->GetCachedAddresses(start_addr, !step_out, stepAddrs);

    if (!rc)
    {
        if (nullptr != addr_count)
        {
            *addr_count = 0;
        }

        return HWDBGINFO_E_NOTFOUND;
    }

    // Output the addresses:
    size_t stepAddrCount = stepAddrs.size();

    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;
    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY(stepAddrCount, addrs, buf_len, err);
    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    void* stepAddrBuf = (void*)(&(stepAddrs[0]));
    HWDBGFAC_INTERFACE_OUTPUT_ARRAY(stepAddrBuf, DwarfAddrType, stepAddrCount, addrs, buf_len, addr_count);

    return HWDBGINFO_E_SUCCESS;
}

// Query a variable for general info:
HwDbgInfo_err hwdbginfo_variable_data(HwDbgInfo_variable var, size_t name_buf_len, char* var_name, size_t* var_name_len, size_t type_name_buf_len, char* type_name, size_t* type_name_len, size_t* var_size, HwDbgInfo_encoding* encoding, bool* is_constant, bool* is_output)
{
    // Parameter validation:
    DbgInfoTwoLevelConsumer::LowLvlVariableInfo* pVar = (DbgInfoTwoLevelConsumer::LowLvlVariableInfo*)var;

    if (nullptr == pVar || (0 == name_buf_len && nullptr == var_name && nullptr == var_name_len && 0 == type_name_buf_len && nullptr == type_name && nullptr == type_name_len && nullptr == var_size && nullptr == encoding && nullptr == is_constant && nullptr == is_output))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(name_buf_len, var_name);
    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(type_name_buf_len, type_name);

    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;
    const std::string& varName = pVar->m_varName;
    size_t nameLen = varName.length();
    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY(nameLen + 1, var_name, name_buf_len, err);

    const std::string& typeName = pVar->m_typeName;
    size_t typeNameLen = typeName.length();
    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY(typeNameLen + 1, type_name, type_name_buf_len, err);
    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    // Output the variable and type names:
    HWDBGFAC_INTERFACE_OUTPUT_STRING(varName, var_name, name_buf_len, var_name_len, err);
    HWDBGFAC_INTERFACE_OUTPUT_STRING(typeName, type_name, type_name_buf_len, type_name_len, err);
    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    // Output the variable size and flags:
    if (nullptr != var_size)
    {
        *var_size = (size_t)pVar->m_varSize;
    }

    if (nullptr != encoding)
    {
        *encoding = pVar->m_varEncoding;
    }

    if (nullptr != is_constant)
    {
        *is_constant = pVar->IsConst();
    }

    if (nullptr != is_output)
    {
        *is_output = pVar->m_isParam && pVar->m_isOutParam;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Queries a variable (with a non-const value) for location information:
HwDbgInfo_err hwdbginfo_variable_location(HwDbgInfo_variable var, HwDbgInfo_locreg* reg_type, unsigned int* reg_num, bool* deref_value, unsigned int* offset, unsigned int* resource, unsigned int* isa_memory_region, unsigned int* piece_offset, unsigned int* piece_size, int* const_add)
{
    // Parameter validation:
    DbgInfoTwoLevelConsumer::LowLvlVariableInfo* pVar = (DbgInfoTwoLevelConsumer::LowLvlVariableInfo*)var;

    if (nullptr == pVar || (nullptr == reg_type && nullptr == reg_num && nullptr == deref_value && nullptr == offset && nullptr == resource && nullptr == isa_memory_region && nullptr == piece_offset && nullptr == piece_size && nullptr == const_add))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    if (pVar->IsConst())
    {
        return HWDBGINFO_E_VARIABLEVALUETYPE;
    }

    DwarfVariableLocation& varLoc = pVar->m_varValue.m_varValueLocation;

    // Output the location details:
    // Register / location type:
    if (nullptr != reg_type)
    {
        *reg_type = (int)varLoc.m_locationRegister;
    }

    // Register number:
    if (nullptr != reg_num)
    {
        *reg_num = varLoc.m_registerNumber;
    }

    // Dereference value flag:
    if (nullptr != deref_value)
    {
        *deref_value = varLoc.m_shouldDerefValue;
    }

    // Location offset
    if (nullptr != offset)
    {
        *offset = varLoc.m_locationOffset;
    }

    // Location resource (ALU):
    if (nullptr != resource)
    {
        *resource = (unsigned int)varLoc.m_locationResource;
    }

    // Location ISA memory region:
    if (nullptr != isa_memory_region)
    {
        *isa_memory_region = varLoc.m_isaMemoryRegion;
    }

    // Value piece offset:
    if (nullptr != piece_offset)
    {
        *piece_offset = varLoc.m_pieceOffset;
    }

    // Value piece size:
    if (nullptr != piece_size)
    {
        *piece_size = varLoc.m_pieceSize;
    }

    // Value const addition:
    if (nullptr != const_add)
    {
        *const_add = varLoc.m_constAddition;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Queries a constant-value variable for it value:
HwDbgInfo_err hwdbginfo_variable_const_value(HwDbgInfo_variable var, size_t buf_size, void* var_value)
{
    // Parameter validation:
    DbgInfoTwoLevelConsumer::LowLvlVariableInfo* pVar = (DbgInfoTwoLevelConsumer::LowLvlVariableInfo*)var;

    if (nullptr == pVar)
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(buf_size, var_value);

    if (!pVar->IsConst())
    {
        return HWDBGINFO_E_VARIABLEVALUETYPE;
    }

    // Output the value:
    if (pVar->m_varSize > buf_size)
    {
        return HWDBGINFO_E_BUFFERTOOSMALL;
    }

    if (nullptr != var_value)
    {
        ::memcpy(var_value, pVar->m_varValue.m_varConstantValue, (size_t)pVar->m_varSize);
    }

    return HWDBGINFO_E_SUCCESS;
}

// Queries a variable for its indirection information:
HwDbgInfo_err hwdbginfo_variable_indirection(HwDbgInfo_variable var, HwDbgInfo_indirection* var_indir, HwDbgInfo_indirectiondetail* var_indir_detail)
{
    // Parameter validation:
    DbgInfoTwoLevelConsumer::LowLvlVariableInfo* pVar = (DbgInfoTwoLevelConsumer::LowLvlVariableInfo*)var;

    if (nullptr == pVar || (nullptr == var_indir && nullptr == var_indir_detail))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    // Output the indirection details:
    if (nullptr != var_indir)
    {
        *var_indir = (int)pVar->m_varIndirection;
    }

    if (nullptr != var_indir_detail)
    {
        *var_indir_detail = (int)pVar->m_varIndirectionDetail;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Queries a variable for its members:
HwDbgInfo_err hwdbginfo_variable_members(HwDbgInfo_variable var, size_t buf_len, HwDbgInfo_variable* members, size_t* member_count)
{
    // Parameter validation:
    DbgInfoTwoLevelConsumer::LowLvlVariableInfo* pVar = (DbgInfoTwoLevelConsumer::LowLvlVariableInfo*)var;

    if (nullptr == pVar || (0 == buf_len && nullptr == members && nullptr == member_count))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(buf_len, members);

    std::vector<DbgInfoTwoLevelConsumer::LowLvlVariableInfo>& memberVector = pVar->m_varMembers;
    size_t memberCount = memberVector.size();
    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;
    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY(memberCount, members, buf_len, err);
    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    // Output the members:
    // Don't add the members to the allocated variable objects vector!
    for (size_t i = 0; i < buf_len; i++)
        if (i < memberCount)
        {
            members[i] = &(memberVector[i]);
        }
        else
        {
            members[i] = nullptr;
        }

    // Output the member count:
    if (nullptr != member_count)
    {
        *member_count = memberCount;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Queries a variable for its address range:
HwDbgInfo_err hwdbginfo_variable_range(HwDbgInfo_variable var, HwDbgInfo_addr* loPC, HwDbgInfo_addr* hiPC)
{
    // Parameter validation:
    DbgInfoTwoLevelConsumer::LowLvlVariableInfo* pVar = (DbgInfoTwoLevelConsumer::LowLvlVariableInfo*)var;

    if (nullptr == pVar || (nullptr == loPC && nullptr == hiPC))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    // Output the range:
    if (nullptr != loPC)
    {
        *loPC = pVar->m_lowVariablePC;
    }

    if (nullptr != hiPC)
    {
        *hiPC = pVar->m_highVariablePC;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Gets a variable from a starting address and a name:
HwDbgInfo_variable hwdbginfo_variable(HwDbgInfo_debug dbg, HwDbgInfo_addr start_addr, bool current_scope_only, const char* var_name, HwDbgInfo_err* err)
{
    // Unused parameter:
    (void)current_scope_only;

    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || nullptr == var_name)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_PARAMETER);
    }

    // Create the output struct:
    DbgInfoTwoLevelConsumer::LowLvlVariableInfo* pVar = new(std::nothrow) DbgInfoTwoLevelConsumer::LowLvlVariableInfo;

    if (nullptr == pVar)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_OUTOFMEMORY);
    }

    // Query the debug info:
    bool rc = pDbg->m_cn->GetVariableInfoInCurrentScope(start_addr, var_name, *pVar);

    if (!rc)
    {
        delete pVar;
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOTFOUND);
    }

    // Add the variable to the allocated variables list:
    pDbg->AddVariable(pVar);

    if (nullptr != err)
    {
        *err = HWDBGINFO_E_SUCCESS;
    }

    return (HwDbgInfo_variable)pVar;
}

// Gets a variable from a starting address and a name:
HwDbgInfo_variable hwdbginfo_low_level_variable(HwDbgInfo_debug dbg, HwDbgInfo_addr start_addr, bool current_scope_only, const char* var_name, HwDbgInfo_err* err)
{
    // Unused parameter:
    (void)current_scope_only;

    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || nullptr == var_name)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_PARAMETER);
    }

    // Debug info type validation:
    if (HwDbgInfo_FacInt_Debug::HWDBGFAC_INTERFACE_TWO_LEVEL_DEBUG_INFO != pDbg->m_tp)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOLLBINARY); // Should this be HWDBGINFO_E_LLINFO?
    }

    HwDbgInfo_FacInt_TwoLevelDebug* pTLDbg = static_cast<HwDbgInfo_FacInt_TwoLevelDebug*>(pDbg);

    // Create the output struct:
    DbgInfoTwoLevelConsumer::LowLvlVariableInfo* pVar = new(std::nothrow) DbgInfoTwoLevelConsumer::LowLvlVariableInfo;

    if (nullptr == pVar)
    {
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_OUTOFMEMORY);
    }

    // Query the debug info:
    bool rc = pTLDbg->ll_cn->GetVariableInfoInCurrentScope(start_addr, var_name, *pVar);

    if (!rc)
    {
        delete pVar;
        HWDBGFAC_INTERFACE_SET_ERR_AND_RETURN_NULL(err, HWDBGINFO_E_NOTFOUND);
    }

    // Add the variable to the allocated variables list:
    pDbg->AddVariable(pVar);

    if (nullptr != err)
    {
        *err = HWDBGINFO_E_SUCCESS;
    }

    return (HwDbgInfo_variable)pVar;
}

// Gets the "local" variables from a starting address virtual stack frame:
HwDbgInfo_err hwdbginfo_frame_variables(HwDbgInfo_debug dbg, HwDbgInfo_addr start_addr, int stack_depth, bool leaf_members, size_t buf_len, HwDbgInfo_variable* vars, size_t* var_count)
{
    // Parameter validation:
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;

    if (nullptr == pDbg || (0 == buf_len && nullptr == vars && nullptr == var_count))
    {
        return HWDBGINFO_E_PARAMETER;
    }

    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_BUFFER(buf_len, vars);

    // Query the debug info:
    // Get a vector of all available variable names from a PC
    std::vector<std::string> varNames;
    bool rc = pDbg->m_cn->ListVariablesFromAddress(start_addr, stack_depth, leaf_members, varNames);

    if (!rc)
    {
        return HWDBGINFO_E_NOTFOUND;
    }

    size_t varCount = varNames.size();
    HwDbgInfo_err err = HWDBGINFO_E_SUCCESS;
    HWDBGFAC_INTERFACE_VALIDATE_OUTPUT_ARRAY(varCount, vars, buf_len, err);
    HWDBGFAC_INTERFACE_CHECKRETURN(err);

    // Get each variable from its name:
    for (size_t i = 0; i < buf_len; i++)
        if (i < varCount)
        {
            vars[i] = hwdbginfo_variable(dbg, start_addr, false, varNames[i].c_str(), &err);

            // The variable name is supposed to be valid:
            if (nullptr == vars[i] && HWDBGINFO_E_SUCCESS == err)
            {
                err = HWDBGINFO_E_UNEXPECTED;
            }

            if (HWDBGINFO_E_SUCCESS != err)
            {
                // Variable could not be found, release previously allocated variables:
                hwdbginfo_release_variables(dbg, vars, i);
                return err;
            }
        }
        else
        {
            vars[i] = nullptr;
        }

    // Output the variable count:
    if (nullptr != var_count)
    {
        *var_count = varCount;
    }

    return HWDBGINFO_E_SUCCESS;
}

// Release the debug info struct:
void hwdbginfo_release_debug_info(HwDbgInfo_debug* dbg)
{
    HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)(*dbg);

    if (nullptr != pDbg)
    {
        delete pDbg;
    }

    *dbg = nullptr;
}

// Release an array of code location objects:
void hwdbginfo_release_code_locations(HwDbgInfo_code_location* locs, size_t loc_count)
{
    assert(nullptr != locs || 0 == loc_count);

    if (nullptr != locs)
        for (size_t i = 0; i < loc_count; i++)
        {
            delete(FileLocation*)(locs[i]);
            locs[i] = nullptr;
        }
}

// Release an array of call stack frames:
void hwdbginfo_release_frame_contexts(HwDbgInfo_frame_context* frames, size_t frame_count)
{
    assert(nullptr != frames || 0 == frame_count);

    if (nullptr != frames)
        for (size_t i = 0; i < frame_count; i++)
        {
            delete(DbgInfoTwoLevelConsumer::TwoLvlCallStackFrame*)(frames[i]);
            frames[i] = nullptr;
        }
}

// Release an array of variables:
void hwdbginfo_release_variables(HwDbgInfo_debug dbg, HwDbgInfo_variable* vars, size_t var_count)
{
    assert(nullptr != vars || 0 == var_count);

    if (nullptr != vars)
    {
        // Cannot release variable objects without a debug structure:
        HwDbgInfo_FacInt_Debug* pDbg = (HwDbgInfo_FacInt_Debug*)dbg;
        assert(nullptr != pDbg);

        if (nullptr == pDbg)
        {
            return;
        }

        // Release each variable:
        for (size_t i = 0; i < var_count; i++)
        {
            // Clear the variable from the debug information structure:
            DbgInfoTwoLevelConsumer::LowLvlVariableInfo* pVar = (DbgInfoTwoLevelConsumer::LowLvlVariableInfo*)(vars[i]);
            bool varWasAllocated = pDbg->RemoveVariable(pVar);

            // If the variable was allocated (i.e. not obtained as a member), delete the object:
            if (varWasAllocated)
            {
                delete pVar;
            }

            vars[i] = nullptr;
        }
    }
}
