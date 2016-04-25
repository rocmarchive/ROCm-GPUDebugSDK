//==============================================================================
// Copyright (c) 2012-2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Description: Used to convert Dwarf to DbgInfo Structures,
/// all methods are static, this class should only be used to parse the data
/// Afterwards all logic to query the data is in the consumer
/// This class has the following instantiation:
/// - LineType    - DbgInfoLines::FileLocation
/// - AddressType - DwarfAddrType
/// - VarLocationType - DwarfVariableLocation
/// It allows us to parse dwarf to our dbgInfo classes
/// The main function is InitializeWithBinary
/// All methods are static and no initialization is required
//==============================================================================

// [US] 16/11/10: libelf and winnt.h define this macro as two different things.
// Since we do not use the libelf macro, we undefine it before including windows headers,
// which contains windows headers, to avoid a redundant warning.
//==============================================================================
#ifdef SLIST_ENTRY
    #undef SLIST_ENTRY
#endif

/// STL:
#include <queue>
#include <assert.h>
#include <string.h>

/// HSA:
// #include <hsa_dwarf.h> // include this header when it is added to the HSA promoted libraries

/// Local:
#include <DbgInfoDwarfParser.h>
#include <DbgInfoUtils.h>

//@{
/// \def [US] 18/11/10: the BSD implementation of DWARF is missing some values in the header file:
#define DW_DLA_STRING 0x01
#define DW_DLA_LOCDESC 0x03
#define DW_DLA_BLOCK 0x06
#define DW_DLA_DIE 0x08
#define DW_DLA_LINE 0x09
#define DW_DLA_ATTR 0x0a
#define DW_DLA_LIST 0x0f
#define DW_DLA_LOC_BLOCK 0x16
//@}
//@{
/// \def [US] 1/2/12 + [GY] 2/26/13 + [US] 5/9/13: private AMD values for DWARF from hsa_dwarf.h and 
#define DW_AT_AMDIL_address_space 0x3ff1
#define DW_AT_AMDIL_resource 0x3ff2

#define DW_TAG_HSA_argument_scope 0x8000 /* HSA */

#define DW_AT_HSA_is_kernel         0x3000 /* HSA. flag, constant, boolean.  Is this DW_TAG_subprogram a kernel (if not, it's a plain function)? */
#define DW_AT_HSA_is_outParam       0x3001 /* HSA. flag, constant, boolean.  Is this DW_TAG_formal_parameter an output parameter? */
#define DW_AT_HSA_workitemid_offset 0x3002 /* Used for ISA DWARF only */
#define DW_AT_HSA_isa_memory_region 0x3003 /* Used for ISA DWARF only */
#define DW_AT_HSA_brig_offset       0x3004 /* Used for ISA DWARF only */

#define DW_LANG_HSA_Assembly      0x9000
/* values for DWARF DW_AT_address_class */
enum Amd_HSA_address_class
{
    Amd_HSA_Private = 0,
    Amd_HSA_Global = 1,
    Amd_HSA_Readonly = 2,
    Amd_HSA_Group = 3
};
//@}

/// Define these as a macro rather than a function to have the HWDBG_ASSERT show the correct file / line:
#define HWDBG_DW_REPORT_ERROR(dwErr, cond)              \
    {                                                   \
        std::string errMsg;                             \
        std::string dwErrMsg;                           \
        if (nullptr != dwErr.err_msg)                   \
        {                                               \
            dwErrMsg = dwErr.err_msg;                   \
        }                                               \
        std::string dwErrFunc;                          \
        if (nullptr != dwErr.err_func)                  \
        {                                               \
            dwErrFunc = dwErr.err_func;                 \
        }                                               \
        \
        errMsg = string_format("Dwarf Error #%d (ELF #%d) at ", dwErr.err_error, dwErr.err_elferror);   \
        ((errMsg += dwErrFunc) += string_format(" (line %d):\n", dwErr.err_line)) +=dwErrMsg;           \
        HWDBG_ASSERT_EX(cond, errMsg);                                                                  \
    }

using namespace HwDbg;

/// -----------------------------------------------------------------------------------------------
/// Initialize
/// \brief Description: Init function - as cannot have constructor in Union
/// \return void
/// -----------------------------------------------------------------------------------------------
void DwarfVariableLocation::Initialize()
{
    m_locationRegister = LOC_REG_UNINIT;
    m_registerNumber = UINT_MAX;
    m_shouldDerefValue = false;
    m_locationOffset = 0;
    m_locationResource = ULLONG_MAX;
    m_isaMemoryRegion = UINT_MAX;
    m_pieceOffset = 0;
    m_pieceSize = UINT_MAX;
    m_constAddition = 0;
}

/// -----------------------------------------------------------------------------------------------
/// DwarfVariableLocation::AsString
/// \brief Description: Static print function to be passed to printer
/// \param[in]          loc - Object to convert to string
/// \param[out]         o_outputString - output string
/// \return void
/// -----------------------------------------------------------------------------------------------
void DwarfVariableLocation::AsString(const DwarfVariableLocation& loc, std::string& o_outputString)
{
    std::string locReg;
    LocRegToStr(loc.m_locationRegister, locReg);
    o_outputString = locReg;
    o_outputString += string_format(" (Reg #%ld, Indirect? %c, Offset %#lx, Resource %lld, ISA Mem Region %ld, Piece Offset %#lx, Piece Size %#lx, Const addition %d)",
                                    loc.m_registerNumber,
                                    loc.m_shouldDerefValue ? 'y' : 'n',
                                    loc.m_locationOffset,
                                    loc.m_locationResource,
                                    loc.m_isaMemoryRegion,
                                    loc.m_pieceOffset,
                                    loc.m_pieceSize,
                                    loc.m_constAddition);
}

/// -----------------------------------------------------------------------------------------------
/// DwarfVariableLocation::LocTypeToStr
/// \brief Description: converts the ValueLocationType to string
/// \param[in]          locType - enum
/// \param[out]         o_outputString - output string
/// \return void
/// -----------------------------------------------------------------------------------------------
void DwarfVariableLocation::LocRegToStr(const DwarfVariableLocation::LocationRegister& locType, std::string& o_outputString)
{
    switch (locType)
    {
        case LOC_REG_REGISTER:
            o_outputString = "Register";
            break;

        case LOC_REG_STACK:
            o_outputString = "Frame pointer register";
            break;

        case LOC_REG_NONE:
            o_outputString = "No register";
            break;

        case LOC_REG_UNINIT:
            o_outputString = "Uninitialized";
            break;

        default:
            o_outputString = "Unexpected Value!";
            break;
    };
}

/// -----------------------------------------------------------------------------------------------
/// FillLineMappingFromDwarf
/// \brief Description: Initialize the line number to address mapping
/// \param[in]          cuDIE - Input die
/// \param[in]          firstSourceFileRealPath - default file from which to get the line numbers
/// \param[in]          pDwarf - allocation/deallocation object
/// \param[in]          err - error container
/// \param[out]         o_lineNumberMapping - Line number mapping
/// \return True : Success
/// \return False: Failure
/// -----------------------------------------------------------------------------------------------
bool DbgInfoDwarfParser::FillLineMappingFromDwarf(Dwarf_Die cuDIE, const std::string& firstSourceFileRealPath, Dwarf_Debug pDwarf, DwarfLineMapping& o_lineNumberMapping)
{
    bool retVal = false;
    Dwarf_Error err = {0};
    // Get the source lines data:
    Dwarf_Line* pLines = nullptr;
    Dwarf_Signed numberOfLines = 0;
    int rc = dwarf_srclines(cuDIE, &pLines, &numberOfLines, &err);

    if (rc == DW_DLV_OK)
    {
        retVal = true;
        bool useRealFirstFile = firstSourceFileRealPath.empty();

        // Iterate the lines:
        for (Dwarf_Signed i = 0; i < numberOfLines; i++)
        {
            // Ignore end sequence "lines", since they simply mark the line number for the end of a scope:
            Dwarf_Bool isESEQ = 0;
            rc = dwarf_lineendsequence(pLines[i], &isESEQ, &err);

            if ((rc == DW_DLV_OK) && (isESEQ == 0))
            {
                // The first file is the kernel main source. We get its path from the OpenCL spy,
                // so we do not need the temp file path from the compiler:
                Dwarf_Unsigned fileIndex = 0;
                std::string sourceFilePathAsString;
                rc = dwarf_line_srcfileno(pLines[i], &fileIndex, &err);

                if ((rc == DW_DLV_OK) && (fileIndex > 0))
                {
                    // Get the source file path:
                    char* fileNameAsCharArray = nullptr;
                    rc = dwarf_linesrc(pLines[i], &fileNameAsCharArray, &err);

                    if ((rc == DW_DLV_OK) && (fileNameAsCharArray != nullptr))
                    {
                        // Convert it to a std::string:
                        sourceFilePathAsString = fileNameAsCharArray;
                        dwarf_dealloc(pDwarf, (Dwarf_Ptr)fileNameAsCharArray, DW_DLA_STRING);
                    }
                }

                // Replace the temp file path with the input from the user if requested:
                if ((!useRealFirstFile) && (1 == fileIndex))
                {
                    sourceFilePathAsString = firstSourceFileRealPath;
                }

                // Get the current line number:
                Dwarf_Unsigned lineNum = 0;
                rc = dwarf_lineno(pLines[i], &lineNum, &err);

                if (rc == DW_DLV_OK)
                {
                    // Get the current line address:
                    Dwarf_Addr lineAddress = 0;
                    rc = dwarf_lineaddr(pLines[i], &lineAddress, &err);

                    if (rc == DW_DLV_OK)
                    {
                        // Connect the file location to the address:
                        size_t pos = 0;
#ifdef _WIN32
#define replaceChar '/'
#define replaceWith '\\'
#else
#define replaceChar '\\'
#define replaceWith '/'
#endif

                        while (std::string::npos != (pos = sourceFilePathAsString.find(replaceChar)))
                        {
                            sourceFilePathAsString[pos++] = replaceWith;
                        }

                        FileLocation fileLocation(sourceFilePathAsString, static_cast<HwDbgUInt64>(lineNum));
                        // Success if we have successfully added the mapping or if the address is 0:
                        bool addSucceeded = o_lineNumberMapping.AddLineMapping(fileLocation, static_cast<DwarfAddrType>(lineAddress)) || (0 == lineAddress);
                        HWDBG_ASSERT(addSucceeded);
                    }
                }
            }

            // Release the line struct:
            dwarf_dealloc(pDwarf, (Dwarf_Ptr)pLines[i], DW_DLA_LINE);
        }

        // Release the lines buffer:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)pLines, DW_DLA_LIST);
    }

    return retVal;
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillCodeScopeFromDwarf
/// \brief Description: Internal function which populates a scope object
/// \param[in] programDIE - Input ptr
/// \param[in] firstSourceFileRealPath
/// \param[in] pDwarf - Allocation/Deallocation object ptr
/// \param[in] pParentScope - parent scope null for top level
/// \param[in] scopeType - the type of scope Compilation unit for Top Level
/// \param[out] o_scope - Out param scope to fill
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillCodeScopeFromDwarf(Dwarf_Die programDIE, const std::string& firstSourceFileRealPath, Dwarf_Debug pDwarf, DwarfCodeScope* pParentScope, const DwarfCodeScopeType& scopeType, DwarfCodeScope& o_scope)
{
    o_scope.m_scopeType = scopeType;
    o_scope.m_pParentScope = pParentScope;

    // Get the scopeName:
    FillScopeName(programDIE, pDwarf, o_scope.m_scopeName);

    // Get the hsa data:
    FillScopeHsaData(programDIE, pDwarf, o_scope);

    // Get the address ranges:
    FillAddressRanges(programDIE, pDwarf, o_scope);

    // Get the frame base data
    FillFrameBase(programDIE, pDwarf, o_scope);

    // Iterate over children and fill the scope with them:
    FillChildren(programDIE, firstSourceFileRealPath, pDwarf, o_scope);

    // Intersect the variables in this program:
    o_scope.IntersectVariablesInScope();
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillChildren
/// \brief Description: Fill the child scopes and variables into a scope
/// \param[in] programDIE - Input die
/// \param[in] firstSourceFileRealPath - default source file path
/// \param[in] pDwarf - Allocation/Deallocation object
/// \param[in] err - Error container
/// \param[out] o_scope - Output parameter the scope with the child vars and scopes filled in
/// \return void
/// ---------------------------------------------------------------------------

void DbgInfoDwarfParser::FillChildren(Dwarf_Die programDIE, const std::string& firstSourceFileRealPath, Dwarf_Debug pDwarf, DwarfCodeScope& o_scope)
{
    // Iterate this DIE's Children, and create program and variable data objects for them:
    Dwarf_Die currentChild = nullptr;
    Dwarf_Error err = {0};
    int rc = dwarf_child(programDIE, &currentChild, &err);
    bool goOn = ((rc == DW_DLV_OK) && (currentChild != nullptr));

    while (goOn)
    {
        // Get the current child's DWARF TAG:
        Dwarf_Half currentChildTag = 0;
        rc = dwarf_tag(currentChild, &currentChildTag, &err);
        HWDBG_ASSERT(rc == DW_DLV_OK);

        if (rc == DW_DLV_OK)
        {
            // Check what kind of DIE this is:
            switch (currentChildTag)
            {
                case DW_TAG_array_type:
                case DW_TAG_class_type:
                case DW_TAG_enumeration_type:
                case DW_TAG_member:
                case DW_TAG_pointer_type:
                case DW_TAG_string_type:
                case DW_TAG_structure_type:
                case DW_TAG_typedef:
                case DW_TAG_union_type:
                case DW_TAG_base_type:
                case DW_TAG_const_type:
                {
                    // Type definitions are not currently handled separately - only as a variable's / constant's parameter.
                }
                break;

                case DW_TAG_entry_point:
                case DW_TAG_lexical_block:
                case DW_TAG_inlined_subroutine:
                case DW_TAG_subprogram:
                case DW_TAG_HSA_argument_scope:
                {
                    // Add the child scope:
                    AddChildScope(currentChild, firstSourceFileRealPath, pDwarf, GetScopeTypeFromTAG(currentChildTag), o_scope);
                }
                break;

                case DW_TAG_compile_unit:
                {
                    // We do not expect compilation units to be children of any other DIE type!
                    HWDBG_ASSERT(false);
                }
                break;

                case DW_TAG_formal_parameter:
                case DW_TAG_constant:
                case DW_TAG_enumerator:
                case DW_TAG_variable:
                {
                    // This is a variable/const/parameter, create an object: (note: if this is a const we will need to get the value and call SetConstantValue())
                    bool isConst = false;
                    bool isParam = false;
                    GetVariableValueTypeFromTAG(currentChildTag, isConst, isParam);
                    DwarfVariableInfo* pVariable = new DwarfVariableInfo;

                    // Need to Initialize the location:
                    if (isConst)
                    {
                        pVariable->m_varValue.m_varConstantValue = nullptr;
                    }
                    else // !isConst
                    {
                        pVariable->m_varValue.m_varValueLocation.Initialize();
                        // Set the address to be the upper limit of the scope by default:
                        o_scope.GetHighestAddressInScope(pVariable->m_highVariablePC);
                    }

                    std::vector<DwarfVariableLocation> variableAdditionalLocations;
                    FillVariableWithInformationFromDIE(currentChild, pDwarf, false, *pVariable, variableAdditionalLocations);

                    // Add it to our variables vector:
                    o_scope.m_scopeVars.push_back(pVariable);

                    // Also add any duplicates it has with other locations:
                    int numberOfAdditionalLocations = (int)variableAdditionalLocations.size();
                    // Check that we do not have a const value type AND locations as const vars do not have location:
                    HWDBG_ASSERT((!isConst) || numberOfAdditionalLocations == 0);

                    if ((isConst) || numberOfAdditionalLocations == 0)
                    {
                        for (int i = 0; i < numberOfAdditionalLocations; i++)
                        {
                            // Copy the name and other metadata:
                            DwarfVariableInfo* pVariableAdditionalLocation = new DwarfVariableInfo;
                            *pVariableAdditionalLocation = *pVariable;

                            // Copy the different location:
                            pVariableAdditionalLocation->m_varValue.m_varValueLocation = variableAdditionalLocations[i];

                            // Add the variable to the vector:
                            o_scope.m_scopeVars.push_back(pVariableAdditionalLocation);
                        }
                    }
                }
                break;

                default:
                {
                    // DW_TAG_imported_declaration, DW_TAG_label, DW_TAG_reference_type, DW_TAG_subroutine_type, DW_TAG_unspecified_parameters
                    // DW_TAG_variant, DW_TAG_common_block, DW_TAG_common_inclusion, DW_TAG_inheritance, DW_TAG_module, DW_TAG_ptr_to_member_type
                    // DW_TAG_set_type, DW_TAG_subrange_type, DW_TAG_with_stmt, DW_TAG_access_declaration, DW_TAG_catch_block, DW_TAG_friend
                    // DW_TAG_namelist, DW_TAG_namelist_item, DW_TAG_packed_type, DW_TAG_template_type_parameter = DW_TAG_template_type_param,
                    // DW_TAG_template_value_parameter = DW_TAG_template_value_param, DW_TAG_thrown_type, DW_TAG_try_block, DW_TAG_variant_part,
                    // DW_TAG_volatile_type, DW_TAG_dwarf_procedure, DW_TAG_restrict_type, DW_TAG_interface_type, DW_TAG_namespace,
                    // DW_TAG_imported_module, DW_TAG_unspecified_type, DW_TAG_partial_unit, DW_TAG_imported_unit, DW_TAG_condition,
                    // DW_TAG_shared_type, DW_TAG_type_unit, DW_TAG_rvalue_reference_type, DW_TAG_template_alias
                    // and user types are not currently handled.
                }
                break;
            }; // switch
        } // if (rc == DW_DLV_OK)

        // Get the next child:
        Dwarf_Die nextChild = nullptr;
        rc = dwarf_siblingof(pDwarf, currentChild, &nextChild, &err);

        // Release the current child:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)currentChild, DW_DLA_DIE);

        // Move to the next iteration:
        currentChild = nextChild;
        goOn = ((rc == DW_DLV_OK) && (currentChild != nullptr));
    } // while(goOn)

    // Make sure we didn't stop on an error:
    // [US] 27/1/14: There's a bug in the BSD libDWARF implementation, where dwarf_child() sometimes returns the error code instead
    // or reporting it via the error output parameter. Thus, we need to check it here:
    HWDBG_ASSERT((DW_DLV_NO_ENTRY == rc) || (DW_DLE_NO_ENTRY == rc) || (DW_DLV_OK == rc));
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::AddChildScope
/// \brief Description: Fill a child scope from the die recursively and add it to its parent.
/// Note: in case it is an abstract representation of inline function - the child scope will not be added to its parent
/// \param[in] childDIE - Input die
/// \param[in] firstSourceFileRealPath - default source file path
/// \param[in] pDwarf - Allocation/Deallocation object
/// \param[in] err - Error container
/// \param[in] childScopeType - the type of child scope to add
/// \param[out] o_scope - Output parameter The scope to which we add the child scope
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::AddChildScope(Dwarf_Die childDIE, const std::string& firstSourceFileRealPath, Dwarf_Debug pDwarf, const DwarfCodeScopeType& childScopeType, DwarfCodeScope& o_scope)
{
    bool shouldAddSubprogram = true;
    Dwarf_Error err = {0};

    if (DwarfCodeScope::DID_SCT_INLINED_FUNCTION != childScopeType)
    {
        // If this is a subprogram DIE, make sure it is not the abstract representation of
        // an inlined function:
        Dwarf_Bool isInlined = 0;
        int rc = dwarf_attrval_flag(childDIE, DW_AT_inline, &isInlined, &err);

        if ((rc == DW_DLV_OK) && (isInlined == 1))
        {
            shouldAddSubprogram = false;
        }
    }

    if (shouldAddSubprogram)
    {
        DwarfCodeScope* pChildScope = new DwarfCodeScope;
        pChildScope->m_scopeType = childScopeType;

        if (DwarfCodeScope::DID_SCT_INLINED_FUNCTION == childScopeType)
        {
            FillInlinedFunctionData(childDIE, firstSourceFileRealPath, pDwarf, *pChildScope);
        }

        // Recursively fill the program objects:
        FillCodeScopeFromDwarf(childDIE, firstSourceFileRealPath, pDwarf, &o_scope, childScopeType, *pChildScope);

        // Add the child scope:
        o_scope.m_children.push_back(pChildScope);
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillAddressRanges
/// \brief Description: Fill the address ranges vector from the dwarf die
/// \param[in] programDIE - the input data
/// \param[in] pDwarf - allocation/deallocation object
/// \param[out] o_addrRanges - out parameter container which holds the ranges
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillAddressRanges(Dwarf_Die programDIE, Dwarf_Debug pDwarf, DwarfCodeScope& o_scope)
{
    Dwarf_Error err = {0};
    Dwarf_Addr lowPCVal = 0;
    Dwarf_Addr highPCVal = 0;
    int rcLVal = dwarf_lowpc(programDIE, &lowPCVal, &err);
    int rcHVal = dwarf_highpc(programDIE, &highPCVal, &err);

    // If we got at least one attribute:
    if ((rcLVal == DW_DLV_OK) || (rcHVal == DW_DLV_OK))
    {
        DwarfCodeScopeAddressRange addrRange;
        addrRange.m_minAddr = 0;
        addrRange.m_maxAddr = ULLONG_MAX;

        if (rcLVal == DW_DLV_OK)
        {
            addrRange.m_minAddr = (DwarfAddrType)lowPCVal;
        }
        else
        {
            DwarfAddrType lowParentAddr = 0;

            if (o_scope.m_pParentScope != nullptr)
            {
                o_scope.m_pParentScope->GetLowestAddressInScope(lowParentAddr);
            }

            addrRange.m_minAddr = lowParentAddr;
        }


        // The High address is also invalid if it's below the low address:
        if ((rcHVal == DW_DLV_OK) && (addrRange.m_minAddr <= highPCVal))
        {
            addrRange.m_maxAddr = (DwarfAddrType)highPCVal;
        }
        else
        {
            // If we got a bad value, just clip it to be equal to the min value:
            addrRange.m_maxAddr = addrRange.m_minAddr;

            // If we got no value at all, use the parent's range (or infinity for the CU range)
            if (rcHVal != DW_DLV_OK)
            {
                DwarfAddrType highParentAddr = ULLONG_MAX;

                if (o_scope.m_pParentScope != nullptr)
                {
                    o_scope.m_pParentScope->GetHighestAddressInScope(highParentAddr);
                }

                addrRange.m_maxAddr = (DwarfAddrType)highParentAddr;
            }
        }

        // Add this range to the ranges vector:
        o_scope.m_scopeAddressRanges.push_back(addrRange);
        o_scope.m_scopeHasNonTrivialAddressRanges = true;
    }

    // Look for the ranges attribute, which supplies multiple ranges:
    Dwarf_Attribute rangesAsAttr = nullptr;
    int rcRAtt = dwarf_attr(programDIE, DW_AT_ranges, &rangesAsAttr, &err);

    if ((rcRAtt == DW_DLV_OK) && (rangesAsAttr != nullptr))
    {
        // Form it into a section offset:
        Dwarf_Off rangesOffset = 0;
        int rcROff = dwarf_global_formref(rangesAsAttr, &rangesOffset, &err);

        if (rcROff != DW_DLV_OK)
        {
            // Try reading it as a non-global address:
            rcROff = dwarf_formref(rangesAsAttr, &rangesOffset, &err);

            if (rcROff != DW_DLV_OK)
            {
                // Prior to DWARF version 3, offsets were encoded as U4 or U8:
                Dwarf_Unsigned rangesOffsetAsUnsigned = 0;
                rcROff = dwarf_formudata(rangesAsAttr, &rangesOffsetAsUnsigned, &err);

                if (rcROff == DW_DLV_OK)
                {
                    rangesOffset = (Dwarf_Off)rangesOffsetAsUnsigned;
                }
            }
        }

        if (rcROff == DW_DLV_OK)
        {
            Dwarf_Ranges* pRangesList = nullptr;
            Dwarf_Signed rangesCount = 0;
            int rcRng = dwarf_get_ranges(pDwarf, rangesOffset, &pRangesList, &rangesCount, nullptr, &err);

            if ((rcRng == DW_DLV_OK) && (pRangesList != nullptr) && (rangesCount > 0))
            {
                // Iterate the ranges:
                for (Dwarf_Signed i = 0; i < rangesCount; i++)
                {
                    switch (pRangesList[i].dwr_type)
                    {
                        case DW_RANGES_ENTRY:
                        {
                            DwarfAddrType loAddr = (DwarfAddrType)(pRangesList[i].dwr_addr1);
                            DwarfAddrType hiAddr = (DwarfAddrType)(pRangesList[i].dwr_addr2);

                            if (hiAddr >= loAddr)
                            {
                                // Add the additional ranges:
                                DwarfCodeScopeAddressRange pcRange(loAddr, hiAddr);
                                o_scope.m_scopeAddressRanges.push_back(pcRange);
                                o_scope.m_scopeHasNonTrivialAddressRanges = true;
                            }
                        }
                        break;

                        case DW_RANGES_ADDRESS_SELECTION:
                            // We currently don't handle direct addresses, and these shouldn't be generated:
                            HWDBG_ASSERT(false);
                            break;

                        case DW_RANGES_END:
                            // Ignore the end marker
                            break;

                        default:
                            // Unexpected value!
                            HWDBG_ASSERT(false);
                            break;
                    }
                }

                // Release the range list:
                dwarf_ranges_dealloc(pDwarf, pRangesList, rangesCount);
            }
        }

        // Release the attribute:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)rangesAsAttr, DW_DLA_ATTR);
    }

    // If we have not found anything at all, add my parent's containing range. If that doesn't exist, add (0, INF):
    if (0 == o_scope.m_scopeAddressRanges.size())
    {
        DwarfCodeScopeAddressRange maxPossibleAddrRange;
        maxPossibleAddrRange.m_minAddr = 0;
        maxPossibleAddrRange.m_maxAddr = ULLONG_MAX;

        // This scope has a parent:
        if (nullptr != o_scope.m_pParentScope)
        {
            o_scope.m_pParentScope->GetLowestAddressInScope(maxPossibleAddrRange.m_minAddr);
            o_scope.m_pParentScope->GetHighestAddressInScope(maxPossibleAddrRange.m_maxAddr);
        }

        o_scope.m_scopeAddressRanges.push_back(maxPossibleAddrRange);
        o_scope.m_scopeHasNonTrivialAddressRanges = false;
    }
}


/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillScopeName
/// \brief Description: Fill the name of the scope
/// \param[in] programDIE - Input dwarf ptr
/// \param[in] pDwarf - allocation / deallocation ptr
/// \param[out] o_scopeName - out param containing the scope name
/// \return
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillScopeName(Dwarf_Die programDIE, Dwarf_Debug pDwarf, std::string& o_scopeName)
{
    Dwarf_Error err = {0};
    // Get the name (if available):
    const char* programNameAsCharArray = nullptr;
    int rcNm = dwarf_diename(programDIE, (char**)&programNameAsCharArray, &err);

    if ((rcNm == DW_DLV_OK) && (programNameAsCharArray != nullptr))
    {
        // Copy the name:
        o_scopeName = programNameAsCharArray;

        // Release the string:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)programNameAsCharArray, DW_DLA_STRING);
    }
}

/// Fills the scope hsa data from DWARF
/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillScopeHsaData
/// \brief Description: Fills the scope hsa data from DWARF
/// \param[in] programDIE - Input dwarf ptr
/// \param[in] pDwarf - allocation / deallocation ptr
/// \param[out] o_scope - out param containing the scope with the information
/// \return
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillScopeHsaData(Dwarf_Die programDIE, Dwarf_Debug pDwarf, DwarfCodeScope& o_scope)
{
    Dwarf_Error err = {0};

    // Get Kernel attribute:
    Dwarf_Bool isKernel = 0;

    int rcKernel = dwarf_attrval_flag(programDIE, DW_AT_HSA_is_kernel, &isKernel, &err);

    if (DW_DLV_OK == rcKernel)
    {
        o_scope.m_isKernel = (isKernel != 0);
    }

    // Get the work item offset
    Dwarf_Attribute hsaVarLocDescAsAttribute = nullptr;
    int rcLoc = dwarf_attr(programDIE, DW_AT_HSA_workitemid_offset, &hsaVarLocDescAsAttribute, &err);

    if (DW_DLV_OK == rcLoc)
    {
        Dwarf_Locdesc* pHsaVarLocationDescriptions = nullptr;
        Dwarf_Signed hsaLocationsCount = 0;
        rcLoc = dwarf_loclist(hsaVarLocDescAsAttribute, &pHsaVarLocationDescriptions, &hsaLocationsCount, &err);

        if (DW_DLV_OK == rcLoc)
        {
            // Get the details and fill the variable data with them:
            delete o_scope.m_pWorkitemOffset;
            o_scope.m_pWorkitemOffset = nullptr;

            bool isFirstLocation = true;

            for (int i = 0; i < hsaLocationsCount; i++)
            {
                Dwarf_Loc* pLocationRecord = pHsaVarLocationDescriptions[i].ld_s;

                if (nullptr == pLocationRecord)
                {
                    // The last location in every list longer than 1 is a nullptr entry used to mark the end of the list:
                    HWDBG_ASSERT((hsaLocationsCount > 1) && (i == (hsaLocationsCount - 1)));
                }
                else if (isFirstLocation)
                {
                    o_scope.m_pWorkitemOffset = new DwarfVariableLocation;
                    o_scope.m_pWorkitemOffset->Initialize();

                    // Read all the operations:
                    int numberOfLocationsOperations = (int)pHsaVarLocationDescriptions[i].ld_cents;

                    for (int j = 0; j < numberOfLocationsOperations; j++)
                    {
                        Dwarf_Loc& rCurrentLocationOperation = pLocationRecord[j];
                        UpdateLocationWithDWARFData(rCurrentLocationOperation, *o_scope.m_pWorkitemOffset, false);
                    }

                    isFirstLocation = false;
                }
                else // !isFirstLocation
                {
                    // assume there should only be one location for this attribute at this stage:
                    HWDBG_ASSERT(false);
                }

                dwarf_dealloc(pDwarf, (Dwarf_Ptr)(pHsaVarLocationDescriptions[i].ld_s), DW_DLA_LOC_BLOCK);
                // The locdesc-s are allocated as a single block - released below:
                // dwarf_dealloc(pDwarf, (Dwarf_Ptr)(&pVarLocationDescriptions[i]), DW_DLA_LOCDESC);
            }

            dwarf_dealloc(pDwarf, (Dwarf_Ptr)pHsaVarLocationDescriptions, DW_DLA_LOCDESC);
        }

        // Release the attribute:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)pHsaVarLocationDescriptions, DW_DLA_ATTR);
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillFrameBase
// Description:
/// \param[in] programDIE - Input dwarf ptr
/// \param[in] pDwarf - allocation / deallocation ptr
/// \param[in] err - container for errors
/// \param[in] base
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillFrameBase(Dwarf_Die programDIE, Dwarf_Debug pDwarf, DwarfCodeScope& o_scope)
{
    Dwarf_Error err = {0};

    if (o_scope.m_pFrameBase != nullptr)
    {
        delete o_scope.m_pFrameBase;
        o_scope.m_pFrameBase = nullptr;
    }

    // Get the frame pointer (if available):
    Dwarf_Attribute fbLocDescAsAttribute = nullptr;
    int rcAddr = dwarf_attr(programDIE, DW_AT_frame_base, &fbLocDescAsAttribute, &err);

    if ((rcAddr == DW_DLV_OK) && (fbLocDescAsAttribute != nullptr))
    {
        // Get the location list from the attribute:
        Dwarf_Locdesc* pFramePointerLocation = nullptr;
        Dwarf_Signed numberOfLocations = 0;

        // Before calling the loclist function, make sure the attribute is the correct format (one of the block formats).
        // See BUG365690 - the compiler sometime emits numbers instead of data blocks here, and our DWARF implementation does not
        // verify the input of dwarf_loclist.
        Dwarf_Half attrFormat = DW_FORM_block;
        rcAddr = dwarf_whatform(fbLocDescAsAttribute, &attrFormat, &err);

        if ((DW_DLV_OK == rcAddr) &&
            ((DW_FORM_block == attrFormat) || (DW_FORM_block1 == attrFormat) || (DW_FORM_block2 == attrFormat) || (DW_FORM_block4 == attrFormat)))
        {
            rcAddr = dwarf_loclist(fbLocDescAsAttribute, &pFramePointerLocation, &numberOfLocations, &err);
        }

        if ((rcAddr == DW_DLV_OK) && (pFramePointerLocation != nullptr) && (0 < numberOfLocations))
        {
            if (pFramePointerLocation->ld_s != nullptr)
            {
                o_scope.m_pFrameBase = new DwarfVariableLocation;
                o_scope.m_pFrameBase->Initialize();

                // Get the location:
                UpdateLocationWithDWARFData(pFramePointerLocation->ld_s[0], *o_scope.m_pFrameBase, false);

                // We expect the frame pointer to be stored in a direct register with no added offset:
                HWDBG_ASSERT(o_scope.m_pFrameBase->m_locationRegister == DwarfVariableLocation::LOC_REG_REGISTER);
                HWDBG_ASSERT(!o_scope.m_pFrameBase->m_shouldDerefValue);
                HWDBG_ASSERT(pFramePointerLocation->ld_s->lr_offset == 0);

                // Release the location block:
                dwarf_dealloc(pDwarf, (Dwarf_Ptr)(pFramePointerLocation->ld_s), DW_DLA_LOC_BLOCK);
            }

            // Release the other locations blocks:
            for (Dwarf_Signed i = 1; numberOfLocations > i; i++)
            {
                if (nullptr != pFramePointerLocation[i].ld_s)
                {
                    dwarf_dealloc(pDwarf, (Dwarf_Ptr)(pFramePointerLocation[i].ld_s), DW_DLA_LOC_BLOCK);
                }
            }

            // Release the location list:
            HWDBG_ASSERT(numberOfLocations == 1);
            dwarf_dealloc(pDwarf, (Dwarf_Ptr)pFramePointerLocation, DW_DLA_LOCDESC);
        }
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::GetLocationFromDWARFData
/// \brief Description: Parses the data from the Dwarf_Loc struct to a ValueLocationType object.
/// \param[in] atom - indicates which register this is and whether its direct or indirect
/// \param[in] number1 - the register number to return in the case of stack offset or extended registers
/// \param[out] o_locationType - output Parameter the location type.
/// \return the register number
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::UpdateLocationWithDWARFData(const Dwarf_Loc& locationRegister, DwarfVariableLocation& io_location, bool isMember)
{
    switch (locationRegister.lr_atom)
    {
        // Address:
        case DW_OP_addr:
            io_location.m_locationRegister = DwarfVariableLocation::LOC_REG_NONE;
            io_location.m_registerNumber = UINT_MAX;
            io_location.m_locationOffset = (unsigned int)locationRegister.lr_number;
            io_location.m_shouldDerefValue = true;
            break;

        case DW_OP_deref:
            HWDBG_ASSERT(!io_location.m_shouldDerefValue);
            io_location.m_shouldDerefValue = true;
            break;

        case DW_OP_xderef:
            HWDBG_ASSERT(!io_location.m_shouldDerefValue);
            io_location.m_shouldDerefValue = true;
            io_location.m_locationResource = (HwDbgUInt64)locationRegister.lr_number;
            break;

        // Constant addition:
        case DW_OP_plus_uconst:

            if (!isMember)
            {
                io_location.m_constAddition += (int)locationRegister.lr_number;
            }
            else // isMember
            {
                // On members, this means an offset:
                io_location.m_pieceOffset += (int)locationRegister.lr_number;
            }

            break;

        // Direct Registers:
        case DW_OP_reg0:
        case DW_OP_reg1:
        case DW_OP_reg2:
        case DW_OP_reg3:
        case DW_OP_reg4:
        case DW_OP_reg5:
        case DW_OP_reg6:
        case DW_OP_reg7:
        case DW_OP_reg8:
        case DW_OP_reg9:
        case DW_OP_reg10:
        case DW_OP_reg11:
        case DW_OP_reg12:
        case DW_OP_reg13:
        case DW_OP_reg14:
        case DW_OP_reg15:
        case DW_OP_reg16:
        case DW_OP_reg17:
        case DW_OP_reg18:
        case DW_OP_reg19:
        case DW_OP_reg20:
        case DW_OP_reg21:
        case DW_OP_reg22:
        case DW_OP_reg23:
        case DW_OP_reg24:
        case DW_OP_reg25:
        case DW_OP_reg26:
        case DW_OP_reg27:
        case DW_OP_reg28:
        case DW_OP_reg29:
        case DW_OP_reg30:
        case DW_OP_reg31:
            io_location.m_locationRegister = DwarfVariableLocation::LOC_REG_REGISTER;
            io_location.m_registerNumber = locationRegister.lr_atom - DW_OP_reg0; // Note the above values are contiguous.
            io_location.m_shouldDerefValue = false;
            io_location.m_locationOffset = (unsigned int)locationRegister.lr_offset;
            break;

        // Indirect Registers:
        case DW_OP_breg0:
        case DW_OP_breg1:
        case DW_OP_breg2:
        case DW_OP_breg3:
        case DW_OP_breg4:
        case DW_OP_breg5:
        case DW_OP_breg6:
        case DW_OP_breg7:
        case DW_OP_breg8:
        case DW_OP_breg9:
        case DW_OP_breg10:
        case DW_OP_breg11:
        case DW_OP_breg12:
        case DW_OP_breg13:
        case DW_OP_breg14:
        case DW_OP_breg15:
        case DW_OP_breg16:
        case DW_OP_breg17:
        case DW_OP_breg18:
        case DW_OP_breg19:
        case DW_OP_breg20:
        case DW_OP_breg21:
        case DW_OP_breg22:
        case DW_OP_breg23:
        case DW_OP_breg24:
        case DW_OP_breg25:
        case DW_OP_breg26:
        case DW_OP_breg27:
        case DW_OP_breg28:
        case DW_OP_breg29:
        case DW_OP_breg30:
        case DW_OP_breg31:
            io_location.m_locationRegister = DwarfVariableLocation::LOC_REG_REGISTER;
            io_location.m_registerNumber = locationRegister.lr_atom - DW_OP_breg0; // Note the above values are contiguous.
            io_location.m_locationOffset = (unsigned int)locationRegister.lr_number;
            io_location.m_shouldDerefValue = true;
            break;

        // Extended registers and frame register:
        case DW_OP_regx:
            io_location.m_locationRegister = DwarfVariableLocation::LOC_REG_REGISTER;
            io_location.m_registerNumber = (unsigned int)locationRegister.lr_number;
            io_location.m_locationOffset = (unsigned int)locationRegister.lr_offset;
            io_location.m_shouldDerefValue = false;
            break;

        case DW_OP_fbreg:
            io_location.m_locationRegister = DwarfVariableLocation::LOC_REG_STACK;
            io_location.m_registerNumber = UINT_MAX;
            io_location.m_locationOffset = (unsigned int)locationRegister.lr_number;
            io_location.m_shouldDerefValue = true;
            break;

        case DW_OP_bregx:
            io_location.m_locationRegister = DwarfVariableLocation::LOC_REG_REGISTER;
            io_location.m_registerNumber = (unsigned int)locationRegister.lr_number;
            io_location.m_locationOffset = (unsigned int)locationRegister.lr_number2;
            io_location.m_shouldDerefValue = true;
            break;

        // Piece operations:
        case DW_OP_piece:

            if (io_location.m_pieceSize > (unsigned int)locationRegister.lr_number)
            {
                io_location.m_pieceSize = (unsigned int)locationRegister.lr_number;
            }

            break;

        case DW_OP_bit_piece:

            if (io_location.m_pieceSize > ((unsigned int)locationRegister.lr_number + 7) / 8)
            {
                io_location.m_pieceSize = ((unsigned int)locationRegister.lr_number + 7) / 8;
            }

            io_location.m_pieceOffset += (((unsigned int)locationRegister.lr_number2) + 7) / 8;
            break;

        // Special operations:
        case DW_OP_deref_size:
            HWDBG_ASSERT(!io_location.m_shouldDerefValue);
            io_location.m_shouldDerefValue = true;
            io_location.m_pieceSize = (unsigned int)locationRegister.lr_number;
            break;

        case DW_OP_xderef_size:
            HWDBG_ASSERT(!io_location.m_shouldDerefValue);
            io_location.m_shouldDerefValue = true;
            io_location.m_locationResource = (HwDbgUInt64)locationRegister.lr_number;
            io_location.m_pieceSize = (unsigned int)locationRegister.lr_number2;
            break;

        case DW_OP_nop:
            break;

        // Unsupported cases:
        case DW_OP_lit0:
        case DW_OP_lit1:
        case DW_OP_lit2:
        case DW_OP_lit3:
        case DW_OP_lit4:
        case DW_OP_lit5:
        case DW_OP_lit6:
        case DW_OP_lit7:
        case DW_OP_lit8:
        case DW_OP_lit9:
        case DW_OP_lit10:
        case DW_OP_lit11:
        case DW_OP_lit12:
        case DW_OP_lit13:
        case DW_OP_lit14:
        case DW_OP_lit15:
        case DW_OP_lit16:
        case DW_OP_lit17:
        case DW_OP_lit18:
        case DW_OP_lit19:
        case DW_OP_lit20:
        case DW_OP_lit21:
        case DW_OP_lit22:
        case DW_OP_lit23:
        case DW_OP_lit24:
        case DW_OP_lit25:
        case DW_OP_lit26:
        case DW_OP_lit27:
        case DW_OP_lit28:
        case DW_OP_lit29:
        case DW_OP_lit30:
        case DW_OP_lit31:

        /*        io_location.m_locationRegister = DwarfVariableLocation::LOC_REG_NONE;
                io_location.m_registerNumber = UINT_MAX;
                io_location.m_locationOffset = 0;
                io_location.m_shouldDerefValue = false;
                io_location.m_constAddition locationRegister.lr_atom - DW_OP_lit0; // Note the above values are contiguous.
                break;*/

        case DW_OP_dup:
        case DW_OP_drop:
        case DW_OP_over:
        case DW_OP_pick:
        case DW_OP_swap:
        case DW_OP_rot:
        case DW_OP_abs:
        case DW_OP_and:
        case DW_OP_div:
        case DW_OP_minus:
        case DW_OP_mod:
        case DW_OP_mul:
        case DW_OP_neg:
        case DW_OP_not:
        case DW_OP_or:
        case DW_OP_plus:
        case DW_OP_xor:
        case DW_OP_eq:
        case DW_OP_ge:
        case DW_OP_gt:
        case DW_OP_le:
        case DW_OP_lt:
        case DW_OP_ne:
        case DW_OP_skip:
            // We do not currently support these operations, which require maintaining an expression stack:
            HWDBG_ASSERT(false);
            break;

        default:
            // Unexpected value!
            HWDBG_ASSERT(false);
            break;
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::GetVariableValueTypeFromTAG
/// \brief Description: Translates a DWARF DIE TAG to the value type enumeration
/// \param[in] dwarfTAG - the dwarf tag to convert to variableValueType
/// \return
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::GetVariableValueTypeFromTAG(int dwarfTAG, bool& isConst, bool& isParam)
{
    switch (dwarfTAG)
    {
        case DW_TAG_formal_parameter:
            isConst = false;
            isParam = true;
            break;

        case DW_TAG_variable:
            isConst = false;
            isParam = false;
            break;

        case DW_TAG_constant:
            isConst = true;
            isParam = false;
            break;

        default:
            isConst = false;
            isParam = false;
            HWDBG_ASSERT(false);
            break;
    };
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::GetScopeTypeFromTAG
/// \brief Description: Translates a DWARF DIE TAG to the scope type enumeration
/// \param[in] dwarfTAG - tag to translate
/// \return void
/// ---------------------------------------------------------------------------
DbgInfoDwarfParser::DwarfCodeScopeType DbgInfoDwarfParser::GetScopeTypeFromTAG(int dwarfTAG)
{
    DwarfCodeScopeType retVal = DwarfCodeScope::DID_SCT_COMPILATION_UNIT;

    switch (dwarfTAG)
    {
        case DW_TAG_entry_point:
        case DW_TAG_subprogram:
            retVal = DwarfCodeScope::DID_SCT_FUNCTION;
            break;

        case DW_TAG_inlined_subroutine:
            retVal = DwarfCodeScope::DID_SCT_INLINED_FUNCTION;
            break;

        case DW_TAG_lexical_block:
            retVal = DwarfCodeScope::DID_SCT_CODE_SCOPE;
            break;

        case DW_TAG_compile_unit:
            retVal = DwarfCodeScope::DID_SCT_COMPILATION_UNIT;
            break;

        case DW_TAG_HSA_argument_scope:
            retVal = DwarfCodeScope::DID_SCT_HSA_ARGUMENT_SCOPE;
            break;

        default:
            HWDBG_ASSERT(false);
            break;
    };

    return retVal;
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillInlinedFunctionData
/// \brief Description: Fills an inlined function special parameters from the DIE
/// \param[in] programDIE - input DIE
/// \param[in] firstSourceFileRealPath - default source file path
/// \param[in] pDwarf - Allocation/Deallocation object
/// \param[in] err - Error container
/// \param[out] o_scope - Output Parameter containing the scope of the inlined function.
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillInlinedFunctionData(Dwarf_Die programDIE, const std::string& firstSourceFileRealPath, Dwarf_Debug pDwarf, DwarfCodeScope& o_scope)
{
    Dwarf_Error err = {0};
    // Get the line number:
    Dwarf_Unsigned callLineNumber = 0;
    int rc = dwarf_attrval_unsigned(programDIE, DW_AT_call_line, &callLineNumber, &err);

    if (rc == DW_DLV_OK)
    {
        o_scope.m_inlineInfo.m_inlinedAt.m_lineNum = static_cast<HwDbgUInt64>(callLineNumber);
    }

    // Get the file name:
    Dwarf_Unsigned callFileNumber = 0;
    rc = dwarf_attrval_unsigned(programDIE, DW_AT_call_file, &callFileNumber, &err);

    if ((rc == DW_DLV_OK) && (callFileNumber > 0))
    {
        // If passed by the user, the first file is the kernel main source. We get its path from the OpenCL spy,
        // so we do not need the temp file path from the compiler:
        o_scope.m_inlineInfo.m_inlinedAt.clearFullPath();

        if ((callFileNumber == 1) && (!firstSourceFileRealPath.empty()))
        {
            // Set the real path as input by the user:
            o_scope.m_inlineInfo.m_inlinedAt.setFullPath(firstSourceFileRealPath);
        }
        else if (callFileNumber > 0)
        {
            // Get the CU DIE:
            Dwarf_Die cuDIE = nullptr;
            rc = dwarf_siblingof(pDwarf, nullptr, &cuDIE,  &err);

            if ((rc == DW_DLV_OK) && (cuDIE != nullptr))
            {
                // Get the file names:
                char** pSourceFilesAsCharArrays = nullptr;
                Dwarf_Signed numberOfSourceFiles = -1;
                rc = dwarf_srcfiles(cuDIE, &pSourceFilesAsCharArrays, &numberOfSourceFiles, &err);

                if ((rc == DW_DLV_OK) && (pSourceFilesAsCharArrays != nullptr) && (numberOfSourceFiles > 0))
                {
                    if (numberOfSourceFiles >= (Dwarf_Signed)callFileNumber)
                    {
                        // The index given by the DIE is 1-based:
                        o_scope.m_inlineInfo.m_inlinedAt.setFullPath(pSourceFilesAsCharArrays[callFileNumber - 1]);
                    }

                    // Release the strings:
                    for (Dwarf_Signed i = 0; i < numberOfSourceFiles; i++)
                    {
                        dwarf_dealloc(pDwarf, (Dwarf_Ptr)(pSourceFilesAsCharArrays[i]), DW_DLA_STRING);
                        pSourceFilesAsCharArrays[i] = nullptr;
                    }

                    // Release the string list:
                    dwarf_dealloc(pDwarf, (Dwarf_Ptr)pSourceFilesAsCharArrays, DW_DLA_LIST);
                }

                // Release the CU DIE:
                dwarf_dealloc(pDwarf, (Dwarf_Ptr)cuDIE, DW_DLA_DIE);
            }
        }
    }

    // Get the function name and variables:
    Dwarf_Attribute functionAbstractOriginAsAttribute = nullptr;
    rc = dwarf_attr(programDIE, DW_AT_abstract_origin, &functionAbstractOriginAsAttribute, &err);

    if ((rc == DW_DLV_OK) && (functionAbstractOriginAsAttribute != nullptr))
    {
        // Get the function abstract origin DIE:
        Dwarf_Die functionAbstractOriginDIE = nullptr;
        rc = GetDwarfFormRefDie(functionAbstractOriginAsAttribute, &functionAbstractOriginDIE, &err, pDwarf);

        if ((rc == DW_DLV_OK) && (functionAbstractOriginDIE != nullptr))
        {
            // Get the function name:
            char* pFunctionNameAsString = nullptr;
            rc = dwarf_diename(functionAbstractOriginDIE, &pFunctionNameAsString, &err);

            if ((rc == DW_DLV_OK) && (pFunctionNameAsString != nullptr))
            {
                // Copy the function name:
                o_scope.m_scopeName = pFunctionNameAsString;
                // Release the string:
                dwarf_dealloc(pDwarf, (Dwarf_Ptr)pFunctionNameAsString, DW_DLA_STRING);
            }

            // Release the DIE:
            dwarf_dealloc(pDwarf, (Dwarf_Ptr)functionAbstractOriginDIE, DW_DLA_DIE);
        }

        // Release the attribute:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)functionAbstractOriginAsAttribute, DW_DLA_ATTR);
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::ListVariableRegisterLocations
/// \brief Description:  Returns all the variable locations that are used by any variable
///                at any time in the DWARF table.
/// \param[in] pTopScope - the top scope.
/// \param[in] variableLocations - output parameter with variable locations.
/// \return Success / failure.
/// ---------------------------------------------------------------------------
bool DbgInfoDwarfParser::ListVariableRegisterLocations(const DwarfCodeScope* pTopScope, std::vector<DwarfAddrType>& variableLocations)
{
    bool retVal = false;

    // Clear the output vector:
    variableLocations.clear();

    if (pTopScope != nullptr)
    {
        retVal = true;

        // Start with the top scope:
        std::queue<const DwarfCodeScope*> scopesToCheck;
        scopesToCheck.push(pTopScope);

        // Go over all the programs in the tree:
        while (!scopesToCheck.empty())
        {
            // Get the current program:
            const DwarfCodeScope* pCurrentScope = scopesToCheck.front();
            HWDBG_ASSERT(pCurrentScope != nullptr);

            if (pCurrentScope != nullptr)
            {
                // Add this program's variables to the locations list:
                const std::vector<DwarfVariableInfo*>& currentScopeVariables = pCurrentScope->m_scopeVars;
                int numberOfVariables = (int)currentScopeVariables.size();

                for (int i = 0; i < numberOfVariables; i++)
                {
                    // Sanity check:
                    DwarfVariableInfo* pCurrentVariable = currentScopeVariables[i];

                    if (pCurrentVariable != nullptr)
                    {
                        if (!pCurrentVariable->IsConst())
                        {
                            // If this variable is placed inside a register:
                            DwarfVariableLocation loc = pCurrentVariable->m_varValue.m_varValueLocation;

                            if (loc.m_locationRegister == DwarfVariableLocation::LOC_REG_REGISTER)
                            {
                                // If we have a location:
                                if (loc.m_registerNumber != UINT_MAX)
                                {
                                    // Add it to the vector:
                                    variableLocations.push_back(loc.m_registerNumber);
                                }
                            }
                        }
                    }
                }

                // If this program has a stack pointer:
                if (pCurrentScope->m_pFrameBase != nullptr && pCurrentScope->m_pFrameBase->m_registerNumber != UINT_MAX)
                {
                    // Add it to the vector:
                    variableLocations.push_back(pCurrentScope->m_pFrameBase->m_registerNumber);
                }

                // Add this program's children to the queue:
                const std::vector<DwarfCodeScope*>& currentChildScopes = pCurrentScope->m_children;
                int numberOfChildren = (int)currentChildScopes.size();

                for (int i = 0; i < numberOfChildren; i++)
                {
                    scopesToCheck.push(currentChildScopes[i]);
                }
            }

            // Dispose of the used program:
            scopesToCheck.pop();
        }
    }

    return retVal;
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillTypeNameAndDetailsFromTypeDIE
/// \brief Description: Fills the DwarfVariableInfo from the die (mainly type information)
/// \param[in] typeDIE - input DIE
/// \param[in] pDwarf - Allocation/Deallocation object
/// \param[in] err - error object
/// \param[in] expandIndirectMembers - whether to expand indirect members
/// \param[in] isRegisterParamter - in this case we set the location type of additional locations to be INDIRECT_REGISTER
/// \param[out] o_variable - output parameter variable
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillTypeNameAndDetailsFromTypeDIE(Dwarf_Die typeDIE, Dwarf_Debug pDwarf, bool expandIndirectMembers, bool isRegisterParamter, DwarfVariableInfo& o_variable)
{
    Dwarf_Error err = {0};
    o_variable.m_typeName.clear();
    o_variable.m_varEncoding = HWDBGINFO_VENC_NONE;
    bool encodingKnown = false;
    o_variable.m_varIndirection = HWDBGINFO_VIND_DIRECT;
    bool goOn = true;
    bool getSibling = false;
    Dwarf_Die currentType = typeDIE;
    Dwarf_Die typeForName = currentType;
    bool foundName = false;
    bool hasMembers = false;
    bool foundIndirectionDetail = false;

    while (goOn)
    {
        // Get the current type's TAG:
        Dwarf_Half currentTypeTag = 0;
        int rc = dwarf_tag(currentType, &currentTypeTag, &err);

        if (rc == DW_DLV_OK)
        {
            // Try getting the address class of reference and pointer types:
            if ((DW_TAG_pointer_type == currentTypeTag) || (DW_TAG_reference_type == currentTypeTag) || (DW_TAG_array_type == currentTypeTag))
            {
                // Get the address class:
                Dwarf_Unsigned addressClassAsDWUnsigned = (Dwarf_Unsigned) - 1;
                rc = dwarf_attrval_unsigned(currentType, DW_AT_address_class, &addressClassAsDWUnsigned, &err);

                if (DW_DLV_OK == rc)
                {
                    foundIndirectionDetail = true;

                    switch (addressClassAsDWUnsigned)
                    {
                        case Amd_HSA_Private:
                            o_variable.m_varIndirectionDetail = HWDBGINFO_VINDD_AMD_GPU_PRIVATE_POINTER;
                            break;

                        case Amd_HSA_Global:
                            o_variable.m_varIndirectionDetail = HWDBGINFO_VINDD_AMD_GPU_GLOBAL_POINTER;
                            break;

                        case Amd_HSA_Readonly:
                            o_variable.m_varIndirectionDetail = HWDBGINFO_VINDD_AMD_GPU_CONSTANT_POINTER;
                            break;

                        case Amd_HSA_Group:
                            o_variable.m_varIndirectionDetail = HWDBGINFO_VINDD_AMD_GPU_LDS_POINTER;
                            break;

                        /*
                        case Amd_HSA_Region:
                            o_variable.m_varIndirectionDetail = HWDBGINFO_VINDD_AMD_GPU_GDS_POINTER;
                            break;
                        */

                        default:
                            // Unexpected Value!
                            HWDBG_ASSERT(false);
                            foundIndirectionDetail = false;
                            break;
                    }
                }
            }

            // See what kind of type this is:
            switch (currentTypeTag)
            {
                case DW_TAG_array_type:

                    // Array, add brackets:
                    if (!foundName)
                    {
                        string_prepend(o_variable.m_typeName, "[]");
                    }

                    // Only change the indirection for the first non direct type:
                    if (HWDBGINFO_VIND_DIRECT == o_variable.m_varIndirection)
                    {
                        o_variable.m_varIndirection = HWDBGINFO_VIND_ARRAY;
                    }

                    break;

                case DW_TAG_enumeration_type:

                    // This is an enumeration, show this in the name and stop looking
                    if (!foundName)
                    {
                        string_prepend(o_variable.m_typeName, " enum");
                    }

                    goOn = false;
                    break;

                case DW_TAG_pointer_type:

                    // Pointer, add an asterisk:
                    if (!foundName)
                    {
                        string_prepend(o_variable.m_typeName, "*");
                    }

                    // Only change the indirection for the first non direct type:
                    if (HWDBGINFO_VIND_DIRECT == o_variable.m_varIndirection)
                    {
                        o_variable.m_varIndirection = HWDBGINFO_VIND_POINTER;
                    }

                    break;

                case DW_TAG_reference_type:

                    // Reference, add an ampersand:
                    if (!foundName)
                    {
                        string_prepend(o_variable.m_typeName, "&");
                    }

                    // Only change the indirection for the first non direct type:
                    if (HWDBGINFO_VIND_DIRECT == o_variable.m_varIndirection)
                    {
                        o_variable.m_varIndirection = HWDBGINFO_VIND_REFERENCE;
                    }

                    break;

                case DW_TAG_structure_type:
                case DW_TAG_union_type:

                    // This is a struct, it will not point to another type, and we'll not show its "value":
                    encodingKnown = true;
                    hasMembers = true;

                    if (foundName)
                    {
                        goOn = false;
                    }
                    else
                    {
                        // Try and see if this struct has a typedef sibling:
                        getSibling = true;
                    }

                    break;

                case DW_TAG_typedef:
                    // This is a typedef, we need to get the name from here but the rest of the details from the pointed value:
                    typeForName = currentType;
                    foundName = true;
                    break;

                case DW_TAG_base_type:
                    // This is a basic type, stop searching and get this type's name:
                    goOn = false;
                    break;

                case DW_TAG_const_type:
                case DW_TAG_volatile_type:
                    // This is a modified type, we currently don't reflect this in the type name
                    // It is possible to add the modifier word (e.g. "const") between the next type element (e.g. int *const*) at this point.
                    break;

                case DW_TAG_class_type:
                case DW_TAG_string_type:
                case DW_TAG_subroutine_type:
                case DW_TAG_ptr_to_member_type:
                case DW_TAG_set_type:
                case DW_TAG_subrange_type:
                case DW_TAG_packed_type:
                case DW_TAG_thrown_type:
                case DW_TAG_restrict_type:
                case DW_TAG_interface_type:
                case DW_TAG_unspecified_type:
                case DW_TAG_shared_type:
                {
                    // Unsupported type:
                    std::string errMsg = "Unsupported kernel variable type";
                    HWDBG_ASSERT_EX(false, errMsg);
                }

                goOn = false;
                break;

                default:
                    // Unexpected value:
                    HWDBG_ASSERT(false);
                    goOn = false;
                    break;
            }

            // If we need to continue
            if (goOn)
            {
                // We want to stop if we fail to iterate:
                goOn = false;

                // Get the next type DIE by this:
                int rcTp = DW_DLV_ERROR;
                Dwarf_Die nextTypeDIE = nullptr;
                bool isValidDIE = false;

                // Avoid using DW_AT_sibling as an attribute, it causes issues with libDWARF:
                if (getSibling)
                {
                    // Get the sibling and verify it is a typedef:
                    rcTp = dwarf_siblingof(pDwarf, currentType, &nextTypeDIE, &err);

                    if ((DW_DLV_OK == rcTp) && (nullptr != nextTypeDIE))
                    {
                        Dwarf_Half nextTypeTag = 0;
                        int rcNT = dwarf_tag(nextTypeDIE, &nextTypeTag, &err);

                        if (rcNT == DW_DLV_OK)
                        {
                            // We need to make sure it's a typedef, otherwise we might
                            // get stuck in a loop:
                            isValidDIE = (nextTypeTag == DW_TAG_typedef);
                        }
                    }
                }
                else // !getSibling
                {
                    Dwarf_Attribute varTypeRefAsAttribute = nullptr;
                    rcTp = dwarf_attr(currentType, DW_AT_type, &varTypeRefAsAttribute, &err);

                    if ((rcTp == DW_DLV_OK) && (varTypeRefAsAttribute != nullptr))
                    {
                        // Get the type DIE
                        rcTp = GetDwarfFormRefDie(varTypeRefAsAttribute, &nextTypeDIE, &err, pDwarf);

                        if ((rcTp == DW_DLV_OK) && (nextTypeDIE != nullptr))
                        {
                            isValidDIE = true;
                        }

                        // Release the attribute:
                        dwarf_dealloc(pDwarf, (Dwarf_Ptr)varTypeRefAsAttribute, DW_DLA_ATTR);
                    }
                }

                // If we didn't go to the sibling or the sibling is proper:
                if (isValidDIE)
                {
                    // Release the previous DIE:
                    if ((currentType != typeDIE) && (currentType != typeForName) && (nullptr != currentType))
                    {
                        dwarf_dealloc(pDwarf, (Dwarf_Ptr)currentType, DW_DLA_DIE);
                    }

                    // Iterate to the next one:
                    currentType = nextTypeDIE;
                    goOn = true;
                }
                else // !isValidDIE
                {
                    // Release the next DIE:
                    if ((nextTypeDIE != typeDIE) && (nextTypeDIE != typeForName) && (nullptr != nextTypeDIE))
                    {
                        dwarf_dealloc(pDwarf, (Dwarf_Ptr)nextTypeDIE, DW_DLA_DIE);
                    }
                }

                // Only attempt to get the sibling once:
                getSibling = false;
            }
        }
        else // rc != DW_DLV_OK
        {
            // We encountered an error:
            goOn = false;
        }
    }

    if (!foundIndirectionDetail)
    {
        // Fill the var indirection details - e.g. pointer address space or array size
        FillVarIndirectionDetails(currentType, o_variable);
    }

    // If we did not find anything more detailed, consider a pointer as pointer type:
    if (((o_variable.m_varIndirection == HWDBGINFO_VIND_POINTER) || (o_variable.m_varIndirection == HWDBGINFO_VIND_ARRAY))
        && (o_variable.m_varEncoding == HWDBGINFO_VENC_NONE) && (!encodingKnown))
    {
        o_variable.m_varEncoding = HWDBGINFO_VENC_POINTER;
    }

    if (!foundName)
    {
        typeForName = currentType;
    }

    // We do not want to expand indirect members in some cases, to avoid recursive loops in this parsing code:
    if ((o_variable.m_varEncoding == HWDBGINFO_VENC_POINTER) && (!expandIndirectMembers))
    {
        hasMembers = false;
    }

    // Get the name of this type:
    const char* typeNameAsCharArray = nullptr;
    int rcNm = dwarf_diename(typeForName, (char**)&typeNameAsCharArray, &err);

    if ((rcNm == DW_DLV_OK) && (typeNameAsCharArray != nullptr))
    {
        // Copy the name:
        std::string baseTypeName;
        baseTypeName = typeNameAsCharArray;
        string_prepend(o_variable.m_typeName, baseTypeName);

        // Release the string:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)typeNameAsCharArray, DW_DLA_STRING);
    }

    // We can now deallocate the type for name DIE:
    if ((currentType != typeForName) && (typeDIE != typeForName) && (nullptr != typeForName))
    {
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)typeForName, DW_DLA_DIE);
    }

    // Get the data size:
    Dwarf_Unsigned typeSizeAsDWARFUnsigned = 0;
    int rcSz = dwarf_attrval_unsigned(currentType, DW_AT_byte_size, &typeSizeAsDWARFUnsigned, &err);

    if (rcSz == DW_DLV_OK)
    {
        o_variable.m_varSize = (HwDbgUInt64)typeSizeAsDWARFUnsigned;
    }

    // Get the encoding for types we want to know and for pointers, for dereferencing:
    if (((o_variable.m_varEncoding == HWDBGINFO_VENC_NONE) && (!encodingKnown))
        || o_variable.m_varIndirection != HWDBGINFO_VIND_DIRECT)
    {
        // Get the encoding type:
        FillVarEncoding(currentType, o_variable);
    }

    // If this (struct) type has children (members), get them:
    if (hasMembers)
    {
        // Get the first child:
        Dwarf_Die currentChild = nullptr;
        int rcCh = dwarf_child(currentType, &currentChild, &err);

        if (rcCh == DW_DLV_OK)
        {
            // Iterate all the children:
            while (currentChild != nullptr)
            {
                // Get the current member's data:
                DwarfVariableInfo currentMember;

                // If not const, share the VariableLocation detail with the members:
                bool isConst = o_variable.IsConst();

                if (!isConst)
                {
                    currentMember.m_varValue.m_varValueLocation = o_variable.m_varValue.m_varValueLocation;
                    // See comment in FillVariableWithInformationFromDIE before the call to this function:
                    // When a struct is passed by-value, its data is actually referenced and not direct.
                    currentMember.m_varValue.m_varValueLocation.m_shouldDerefValue = isRegisterParamter || currentMember.m_varValue.m_varValueLocation.m_shouldDerefValue;

                    currentMember.m_lowVariablePC = o_variable.m_lowVariablePC;
                    currentMember.m_highVariablePC = o_variable.m_highVariablePC;
                }

                // Variables do not have locations that are scoped, so ignore them for this:
                std::vector<DwarfVariableLocation> ignoredAdditionalLocations;
                FillVariableWithInformationFromDIE(currentChild, pDwarf, true, currentMember, ignoredAdditionalLocations);
                HWDBG_ASSERT(ignoredAdditionalLocations.size() == 0);

                // If const, we calculate the buffer by taking the parent buffer and adding the member offset:
                if (isConst)
                {
                    size_t memberOffset = currentMember.m_varValue.m_varValueLocation.m_locationOffset;
                    currentMember.SetConstantValue(currentMember.m_varSize, o_variable.m_varValue.m_varConstantValue + memberOffset);
                }

                // Add the member to the vector:
                o_variable.m_varMembers.push_back(currentMember);

                // Move to the next member DIE:
                Dwarf_Die nextChild = nullptr;
                int rcSib = dwarf_siblingof(pDwarf, currentChild, &nextChild, &err);

                // Release the current child:
                dwarf_dealloc(pDwarf, (Dwarf_Ptr)currentChild, DW_DLA_DIE);

                if (DW_DLV_OK == rcSib)
                {
                    // Move to the next iteration:
                    currentChild = nextChild;
                }
                else
                {
                    // Stop on failure:
                    currentChild = nullptr;
                }
            }
        }
    }

    // Release the base type DIE:
    if (currentType != typeDIE)
    {
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)currentType, DW_DLA_ATTR);
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::InitializeWithBinary
/// \brief Description: Main function: Looks for DWARF sections in the supplied ELF binary and initializes the reading from it.
/// \param[in] kernelBinary - the data containing the ELF, including size
/// \param[out] o_scope - Out Param -the top code scope
/// \param[out] o_lineNumberMapping - Out Param the line <-> address mapping
/// \param[in] firstSourceFileRealPath - the path to the original source file for the mapping
/// \return Success / failure.
/// ---------------------------------------------------------------------------
bool DbgInfoDwarfParser::InitializeWithBinary(const KernelBinary& kernelBinary, DwarfCodeScope& o_scope, DwarfLineMapping& o_lineNumberMapping, const std::string& firstSourceFileRealPath)
{
    bool retVal = false;

    // Set the version of elf:
    elf_version(EV_CURRENT);

    // Initialize an Elf object with the buffer:
    Elf* pElf = elf_memory((char*)(kernelBinary.m_pBinaryData), kernelBinary.m_binarySize);
    Dwarf_Error err = {0};
    Dwarf_Debug pDwarf = nullptr;

    HWDBG_ASSERT(pElf != nullptr);

    if (pElf != nullptr)
    {
        // Initialize a D with this Elf object:
        int rcDW = dwarf_elf_init(pElf, DW_DLC_READ, nullptr, nullptr, &pDwarf, &err);

        if ((rcDW == DW_DLV_OK) && (pDwarf != nullptr))
        {
            // Mark that we succeeded:
            retVal = true;

            // Get the offset for the compilation unit. Note that we expect OpenCL kernels
            // to only have one CU. Check that we have one at all:
            Dwarf_Unsigned cuHeaderOffset = 0;

            int rc = dwarf_next_cu_header(pDwarf, nullptr, nullptr, nullptr, nullptr, &cuHeaderOffset, &err);

            if (rc == DW_DLV_OK)
            {
                // Get the DIE for the first CU by calling for the sibling of nullptr:
                Dwarf_Die cuDIE = nullptr;
                rc = dwarf_siblingof(pDwarf, nullptr, &cuDIE, &err);

                if (rc == DW_DLV_OK)
                {
                    FillCodeScopeFromDwarf(cuDIE, firstSourceFileRealPath, pDwarf, nullptr, DwarfCodeScope::DID_SCT_COMPILATION_UNIT, o_scope);

                    // Use the CU DIE to get the line number information. This needs to happen after the programs are
                    // initialized, since each entry must be associated with a program:
                    DwarfLineMapping lineNumberMapping;
                    bool rcLn = FillLineMappingFromDwarf(cuDIE, firstSourceFileRealPath, pDwarf, o_lineNumberMapping);
                    HWDBG_ASSERT(rcLn);

                    // Fill addresses from mapping:
                    std::vector<DwarfAddrType> addresses;
                    o_lineNumberMapping.GetMappedAddresses(addresses);
                    retVal = o_scope.MapAddressesToCodeScopes(addresses);

                    // Release the CU DIE:
                    dwarf_dealloc(pDwarf, (Dwarf_Ptr)cuDIE, DW_DLA_DIE);
                }
            }
        }
        else
        {
            // Report initialization errors:
            HWDBG_DW_REPORT_ERROR(err, false);
        }
    }

    // If we failed, clean up:
    if (!retVal)
    {
        if (pDwarf != nullptr)
        {
            int rcDF = dwarf_finish(pDwarf, &err);
            HWDBG_ASSERT(DW_DLV_OK == rcDF);
        }

        if (pElf != nullptr)
        {
            int rcEF = elf_end(pElf);
            HWDBG_ASSERT(rcEF == 0);
            pElf = nullptr;
        }
    }

    return retVal;
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillVariableWithInformationFromDIE
/// \brief Description: Recursively fills variable information from the DIE
/// \param[in] variableDIE - Input DIE
/// \param[in] pDwarf - Allocation/Deallocation object
/// \param[in] err - Error container
/// \param[in] isMember - whether this variable is a member of another
/// \param[out] o_variableData - Output Param the filled variable
/// \param[out] o_variableAdditionalLocations - additional locations of this same variable added so that we know if we have at least one location.
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillVariableWithInformationFromDIE(Dwarf_Die variableDIE, Dwarf_Debug pDwarf, bool isMember, DwarfVariableInfo& o_variableData, std::vector<DwarfVariableLocation>& o_variableAdditionalLocations)
{
    Dwarf_Error err = {0};
    // Get the variable's location attribute:
    o_variableAdditionalLocations.clear();

    // Get the variable name:
    FillVarName(variableDIE, pDwarf, o_variableData);

    // Check if const:
    bool isConst = o_variableData.IsConst();

    // Locations:
    Dwarf_Attribute varLocDescAsAttribute = nullptr;
    int rc = dwarf_attr(variableDIE, isMember ? DW_AT_data_member_location : DW_AT_location, &varLocDescAsAttribute, &err);

    // Do not go in here if const:
    if ((rc == DW_DLV_OK) && (varLocDescAsAttribute != nullptr) && (!isConst))
    {
        // Get the location list from the attribute:
        Dwarf_Locdesc* pVarLocationDescriptions = nullptr;
        Dwarf_Signed locationsCount = 0;

        // Before calling the loclist function, make sure the attribute is the correct format (one of the block formats).
        // See BUG365690 - the compiler sometime emits numbers instead of data blocks here, and our DWARF implementation does not
        // verify the input of dwarf_loclist.
        Dwarf_Half attrFormat = DW_FORM_block;
        rc = dwarf_whatform(varLocDescAsAttribute, &attrFormat, &err);

        if ((DW_DLV_OK == rc) &&
            ((DW_FORM_block == attrFormat) || (DW_FORM_block1 == attrFormat) || (DW_FORM_block2 == attrFormat) || (DW_FORM_block4 == attrFormat)))
        {
            rc = dwarf_loclist(varLocDescAsAttribute, &pVarLocationDescriptions, &locationsCount, &err);
        }

        if ((rc == DW_DLV_OK) && (pVarLocationDescriptions != nullptr))
        {
            // Get the start scope attribute, if it is available:
            Dwarf_Unsigned startScopeAsDwarfUnsigned = 0;
            int rcHasStartScope = dwarf_attrval_unsigned(variableDIE, DW_AT_start_scope, &startScopeAsDwarfUnsigned, &err);
            DwarfAddrType startScope = 0;

            if (rcHasStartScope == DW_DLV_OK)
            {
                // Make sure this start scope is between the given addresses:
                startScope = static_cast<DwarfAddrType>(startScopeAsDwarfUnsigned);
            }

            // Get the resource attribute, if it is available:
            Dwarf_Unsigned resourceAsDwarfUnsigned = 0;
            int rcHasResource = dwarf_attrval_unsigned(variableDIE, DW_AT_AMDIL_resource, &resourceAsDwarfUnsigned, &err);

            // If this variable has at least one location:
            bool isFirstLocation = true;

            for (int i = 0; i < locationsCount; i++)
            {
                // Get the details and fill the variable data with them:
                DwarfVariableLocation variableCurrentLocation = o_variableData.m_varValue.m_varValueLocation;

                if (rcHasResource == DW_DLV_OK)
                {
                    // Note that board older than SI might not produce this attribute at all:
                    variableCurrentLocation.m_locationResource = (HwDbgUInt64)resourceAsDwarfUnsigned;
                }

                if (!isMember)
                {
                    o_variableData.m_lowVariablePC = static_cast<DwarfAddrType>(pVarLocationDescriptions[i].ld_lopc);
                    o_variableData.m_highVariablePC = static_cast<DwarfAddrType>(pVarLocationDescriptions[i].ld_hipc);
                }

                if ((rcHasStartScope == DW_DLV_OK) && (o_variableData.m_highVariablePC >= startScope) && (o_variableData.m_lowVariablePC < startScope))
                {
                    // This is more detailed, use it instead:
                    o_variableData.m_lowVariablePC = startScope;
                }

                Dwarf_Loc* pLocationRecord = pVarLocationDescriptions[i].ld_s;

                if (pLocationRecord != nullptr)
                {
                    // Read all the operations:
                    int numberOfLocationsOperations = (int)pVarLocationDescriptions[i].ld_cents;

                    for (int j = 0; j < numberOfLocationsOperations; j++)
                    {
                        Dwarf_Loc& rCurrentLocationOperation = pLocationRecord[j];
                        UpdateLocationWithDWARFData(rCurrentLocationOperation, variableCurrentLocation, isMember);
                    }

                    if (o_variableData.m_varSize > variableCurrentLocation.m_pieceSize)
                    {
                        o_variableData.m_varSize = (HwDbgUInt64)variableCurrentLocation.m_pieceSize;
                    }
                }
                else // pLocationRecord == nullptr
                {
                    // The last location in every list longer than 1 is a nullptr entry used to mark the end of the list:
                    HWDBG_ASSERT((locationsCount > 1) && (i == (locationsCount - 1)));
                }

                if (isFirstLocation)
                {
                    o_variableData.m_varValue.m_varValueLocation = variableCurrentLocation;
                    isFirstLocation = false;
                }
                else // !isFirstLocation
                {
                    o_variableAdditionalLocations.push_back(variableCurrentLocation);
                }

                dwarf_dealloc(pDwarf, (Dwarf_Ptr)(pVarLocationDescriptions[i].ld_s), DW_DLA_LOC_BLOCK);
                // The locdesc-s are allocated as a single block - released below:
                // dwarf_dealloc(pDwarf, (Dwarf_Ptr)(&pVarLocationDescriptions[i]), DW_DLA_LOCDESC);
            }

            dwarf_dealloc(pDwarf, (Dwarf_Ptr)pVarLocationDescriptions, DW_DLA_LOCDESC);
        }

        // Release the attribute:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)varLocDescAsAttribute, DW_DLA_ATTR);
    } // End if (...Location)

    bool isRegisterParameter = false;
    DwarfVariableLocation::LocationRegister locType = o_variableData.m_varValue.m_varValueLocation.m_locationRegister;

    if ((!isConst) && (o_variableData.m_isParam) && (locType == DwarfVariableLocation::LOC_REG_REGISTER))
    {
        isRegisterParameter = true;
    }

    // Get the variable type:
    Dwarf_Attribute varTypeRefAsAttribute = nullptr;
    int rcTp = dwarf_attr(variableDIE, DW_AT_type, &varTypeRefAsAttribute, &err);

    if ((rcTp == DW_DLV_OK) && (varTypeRefAsAttribute != nullptr))
    {
        // Get the type DIE
        Dwarf_Die typeDIE = nullptr;
        rcTp = GetDwarfFormRefDie(varTypeRefAsAttribute, &typeDIE, &err, pDwarf);

        if ((rcTp == DW_DLV_OK) && (typeDIE != nullptr))
        {
            // Get all the type information recursively: (Note: this calls back into our current function for member variables):
            FillTypeNameAndDetailsFromTypeDIE(typeDIE, pDwarf, !isMember, isRegisterParameter, o_variableData);

            // Do not allow members to have no name:
            if (isMember && (o_variableData.m_varName.empty()))
            {
                CreateVarNameFromType(typeDIE, o_variableData);
            }

            // Release the DIE:
            dwarf_dealloc(pDwarf, (Dwarf_Ptr)typeDIE, DW_DLA_DIE);
        }

        // Release the attribute:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)varTypeRefAsAttribute, DW_DLA_ATTR);
    }

    // As per the comment above, if the value of a parameter is a struct, it's actually an indirect value:
    if ((isRegisterParameter) && (o_variableData.m_varEncoding == HWDBGINFO_VENC_NONE))
    {
        // Sanity check that this is not a const:
        HWDBG_ASSERT(!isConst);

        if (!isConst)
        {
            o_variableData.m_varValue.m_varValueLocation.m_shouldDerefValue = true;
        }
    }

    // If the variable has a constant value, get it:
    FillConstValue(variableDIE, pDwarf, o_variableData);

    // Get the function name and variables:
    Dwarf_Attribute variableAbstractOriginAsAttribute = nullptr;
    rc = dwarf_attr(variableDIE, DW_AT_abstract_origin, &variableAbstractOriginAsAttribute, &err);

    if ((rc == DW_DLV_OK) && (variableAbstractOriginAsAttribute != nullptr))
    {
        // Get the function abstract origin DIE:
        Dwarf_Die variableAbstractOriginDIE = nullptr;
        rc = GetDwarfFormRefDie(variableAbstractOriginAsAttribute, &variableAbstractOriginDIE, &err, pDwarf);

        if ((rc == DW_DLV_OK) && (variableAbstractOriginDIE != nullptr))
        {
            // Fill any data you can from the abstract origin. We do not need locations, since the abstract origin has none:
            std::vector<DwarfVariableLocation> ignoredAdditionalLocations;
            FillVariableWithInformationFromDIE(variableAbstractOriginDIE, pDwarf, isMember, o_variableData, ignoredAdditionalLocations);
            HWDBG_ASSERT(ignoredAdditionalLocations.size() == 0);

            // Release the DIE:
            dwarf_dealloc(pDwarf, (Dwarf_Ptr)variableAbstractOriginDIE, DW_DLA_DIE);
        }

        // Release the attribute:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)variableAbstractOriginAsAttribute, DW_DLA_ATTR);
    }

    // Get out param attribute:
    Dwarf_Bool isOutParam = 0;
    int rcOut = dwarf_attrval_flag(variableDIE, DW_AT_HSA_is_outParam, &isOutParam, &err);

    if (DW_DLV_OK == rcOut)
    {
        o_variableData.m_isOutParam = (isOutParam != 0);
    }

    // Get the Memory region
    Dwarf_Unsigned region;

    int rcRegion = dwarf_attrval_unsigned(variableDIE, DW_AT_HSA_isa_memory_region, &region, &err);

    if (DW_DLV_OK == rcRegion)
    {
        o_variableData.m_varValue.m_varValueLocation.m_isaMemoryRegion = (unsigned int)region;
    }

    // Get the Brig offset
    Dwarf_Unsigned brigOffset;

    int rcOS = dwarf_attrval_unsigned(variableDIE, DW_AT_HSA_brig_offset, &brigOffset, &err);

    if (DW_DLV_OK == rcOS)
    {
        o_variableData.m_brigOffset = (unsigned int)brigOffset;
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillVarIndirectionDetails
/// \brief Description: Fill the indirection detail - Pointer address space for pointers
///                Need implementation for references and arrays
/// \param[in] variableDIE - Input DIE
/// \param[in] err - Error container
/// \param[out] o_variable - Output Parameter the variable
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillVarIndirectionDetails(Dwarf_Die variableDIE, DwarfVariableInfo& o_variable)
{
    Dwarf_Error err = {0};

    switch (o_variable.m_varIndirection)
    {
        case HWDBGINFO_VIND_POINTER:
        {
            // Get the address space attribute, if it is available:
            Dwarf_Unsigned addressSpaceAsDwarfUnsigned = 0;
            int rc = dwarf_attrval_unsigned(variableDIE, DW_AT_AMDIL_address_space, &addressSpaceAsDwarfUnsigned, &err);

            if (rc == DW_DLV_OK)
            {
                switch (addressSpaceAsDwarfUnsigned)
                {
                    case 1:
                        o_variable.m_varIndirectionDetail = HWDBGINFO_VINDD_AMD_GPU_GLOBAL_POINTER;
                        break;

                    case 2:
                        o_variable.m_varIndirectionDetail = HWDBGINFO_VINDD_AMD_GPU_CONSTANT_POINTER;
                        break;

                    case 3:
                        o_variable.m_varIndirectionDetail = HWDBGINFO_VINDD_AMD_GPU_LDS_POINTER;
                        break;

                    default:
                        // The attribute is present, but has an unknown value:
                        o_variable.m_varIndirectionDetail = HWDBGINFO_VINDD_AMD_GPU_UNKNOWN_POINTER;
                        break;
                }
            }

            break;
        }

        case HWDBGINFO_VIND_ARRAY:
            break;

        case HWDBGINFO_VIND_REFERENCE:
            break;

        default:
            break;
    };
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillVarEncoding
/// \brief Description: Fills the encoding into the variable
/// \param[in] variableDIE - Input DIE
/// \param[in] err - Error container
/// \param[out] o_variable - Output Parameter Variable
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillVarEncoding(Dwarf_Die variableDIE, DwarfVariableInfo& o_variable)
{
    Dwarf_Error err = {0};
    // Get the encoding type:
    Dwarf_Unsigned typeEncodingAsDWARFUnsigned = 0;
    int rcFm = dwarf_attrval_unsigned(variableDIE, DW_AT_encoding, &typeEncodingAsDWARFUnsigned, &err);

    if (rcFm == DW_DLV_OK)
    {
        switch (typeEncodingAsDWARFUnsigned)
        {
            case DW_ATE_address:
                o_variable.m_varEncoding = HWDBGINFO_VENC_POINTER;
                break;

            case DW_ATE_boolean:
                o_variable.m_varEncoding = HWDBGINFO_VENC_BOOLEAN;
                break;

            case DW_ATE_float:
                o_variable.m_varEncoding = HWDBGINFO_VENC_FLOAT;
                break;

            case DW_ATE_signed:
                o_variable.m_varEncoding = HWDBGINFO_VENC_INTEGER;
                break;

            case DW_ATE_signed_char:
                o_variable.m_varEncoding = HWDBGINFO_VENC_CHARACTER;
                break;

            case DW_ATE_unsigned:
                o_variable.m_varEncoding = HWDBGINFO_VENC_UINTEGER;
                break;

            case DW_ATE_unsigned_char:
                o_variable.m_varEncoding = HWDBGINFO_VENC_UCHARACTER;
                break;

            case DW_ATE_complex_float:
            case DW_ATE_imaginary_float:
            case DW_ATE_packed_decimal:
            case DW_ATE_numeric_string:
            case DW_ATE_edited:
            case DW_ATE_signed_fixed:
            case DW_ATE_unsigned_fixed:
            case DW_ATE_decimal_float:
            {
                // Unsupported type:
                std::string errMsg = "Unsupported kernel variable type";
                HWDBG_ASSERT_EX(false, errMsg);
            }
            break;

            default:
                // Unexpected value:
                HWDBG_ASSERT(false);
                break;
        }
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillVarName
/// \brief Description: Fills the variable name from dwarf
/// \param[in] variableDIE - Input Parameter
/// \param[in] pDwarf - Allocation/Deallocation object
/// \param[in] err - Error container
/// \param[out] o_variable - Output parameter variable info
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillVarName(Dwarf_Die variableDIE, Dwarf_Debug pDwarf, DwarfVariableInfo& o_variable)
{
    Dwarf_Error err = {0};
    // Get the variable name:
    const char* varNameAsCharArray = nullptr;
    int rcNm = dwarf_diename(variableDIE, (char**)&varNameAsCharArray, &err);

    if ((rcNm == DW_DLV_OK) && (varNameAsCharArray != nullptr))
    {
        // Copy the name:
        o_variable.m_varName = varNameAsCharArray;

        // Release the string:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)varNameAsCharArray, DW_DLA_STRING);
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::CreateVarNameFromType
/// \brief Description: Generate a var name from the type information
/// \param[in] typeDIE - Input Parameter
/// \param[in] err - Error container
/// \param[out] o_variable - Output parameter variable info
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::CreateVarNameFromType(Dwarf_Die typeDIE, DwarfVariableInfo& o_variable)
{
    Dwarf_Error err = {0};
    o_variable.m_varName = "unnamed_";
    Dwarf_Half currentTypeTag = 0;
    int rc = dwarf_tag(typeDIE, &currentTypeTag, &err);

    if (DW_DLV_OK == rc)
    {
        // See what kind of type this is:
        switch (currentTypeTag)
        {
            case DW_TAG_array_type:
                o_variable.m_varName += "array_";
                break;

            case DW_TAG_enumeration_type:
                o_variable.m_varName += "enum_";
                break;

            case DW_TAG_pointer_type:
                o_variable.m_varName += "ptr_";
                break;

            case DW_TAG_reference_type:
                o_variable.m_varName += "ref_";
                break;

            case DW_TAG_structure_type:
                o_variable.m_varName += "struct_";
                break;

            case DW_TAG_union_type:
                o_variable.m_varName += "union_";
                break;

            case DW_TAG_class_type:
                o_variable.m_varName += "class_";
                break;

            case DW_TAG_typedef:
            case DW_TAG_base_type:
            case DW_TAG_const_type:
            case DW_TAG_volatile_type:
            case DW_TAG_string_type:
            case DW_TAG_subroutine_type:
            case DW_TAG_ptr_to_member_type:
            case DW_TAG_set_type:
            case DW_TAG_subrange_type:
            case DW_TAG_packed_type:
            case DW_TAG_thrown_type:
            case DW_TAG_restrict_type:
            case DW_TAG_interface_type:
            case DW_TAG_unspecified_type:
            case DW_TAG_shared_type:
            default:
                o_variable.m_varName += "member_";
                break;
        }
    }
    else // DW_DLV_OK != rc
    {
        o_variable.m_varName += "member_";
    }

    // Add a serial number (spanning between types, etc):
    static int unknownMemberIndex = 0;
    o_variable.m_varName += string_format("%d", unknownMemberIndex++);
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::FillConstValue
/// \brief Description: fill const data from die
/// \param[in] typeDIE - Input Parameter
/// \param[in] pDwarf - Allocation/Deallocation object
/// \param[in] err - Error container
/// \param[out] o_variable - Output parameter variable info
/// \return void
/// ---------------------------------------------------------------------------
void DbgInfoDwarfParser::FillConstValue(Dwarf_Die variableDIE, Dwarf_Debug pDwarf, DwarfVariableInfo& o_variable)
{
    Dwarf_Error err = {0};
    Dwarf_Attribute constValueAttribute = nullptr;
    int rcCV = dwarf_attr(variableDIE, DW_AT_const_value, &constValueAttribute, &err);

    if (rcCV == DW_DLV_OK)
    {
        Dwarf_Block* constValueBlock = nullptr;
        rcCV = dwarf_formblock(constValueAttribute, &constValueBlock, &err);

        if (rcCV == DW_DLV_OK)
        {
            // Copy it into the struct:
            o_variable.SetConstantValue(static_cast<HwDbgUInt64>(constValueBlock->bl_len), static_cast<unsigned char*>(constValueBlock->bl_data));
            // Release the block:
            dwarf_dealloc(pDwarf, (Dwarf_Ptr)constValueBlock, DW_DLA_BLOCK);
        }

        // Release the attribute:
        dwarf_dealloc(pDwarf, (Dwarf_Ptr)constValueAttribute, DW_DLA_ATTR);
    }
}

/// ---------------------------------------------------------------------------
/// DbgInfoDwarfParser::GetDwarfFormRefDie
/// \brief Description: Local function - retrieves a die according to a reference attribute
/// \param[in] attr - reference attribute
/// \param[in] *pReturnDIE - DIE to return
/// \param[in] pError - Error reporting object
/// \param[in] dbg - This added parameter cannot be avoided since we can't get attr's attr->at_die->die_dbg member
/// \return void
/// ---------------------------------------------------------------------------
int DbgInfoDwarfParser::GetDwarfFormRefDie(Dwarf_Attribute attr, Dwarf_Die* pReturnDIE, Dwarf_Error* pError, Dwarf_Debug dbg /* This added parameter cannot be avoided since we can't get attr's attr->at_die->die_dbg member */)
{
    int retVal = DW_DLV_OK;

    // We expect the attribute to be a reference or a global reference. Try each of those:
    Dwarf_Error err = {0};
    Dwarf_Off offset = 0;
    int rc = dwarf_global_formref(attr, &offset, &err);

    if (rc == DW_DLV_OK)
    {
        // Get the DIE:
        retVal = dwarf_offdie(dbg, offset, pReturnDIE, pError);
    }
    else // rc != DW_DLV_OK
    {
        // Not a global reference, try local (global offset should cover what we're doing manually here):
        rc = dwarf_formref(attr, &offset, &err);

        if (rc == DW_DLV_OK)
        {
            // Get the compilation unit's offset:
            Dwarf_Die cuDIE = nullptr;
            dwarf_siblingof(dbg, nullptr, &cuDIE, nullptr);
            Dwarf_Off cuOff = 0;
            dwarf_dieoffset(cuDIE, &cuOff, nullptr);
            HWDBG_ASSERT(nullptr != cuDIE);

            if (nullptr != cuDIE)
            {
                dwarf_dealloc(dbg, (Dwarf_Ptr)cuDIE, DW_DLA_DIE);
            }

            cuDIE = nullptr;

            // Add the CU offset to this offset, and get the DIE:
            retVal = dwarf_offdie(dbg, offset + cuOff, pReturnDIE, pError);
        }
        else // rc != DW_DLV_OK
        {
            // Return the error and retVal from the form function:
            retVal = rc;

            if (pError != nullptr)
            {
                *pError = err;
            }
        }
    }

    return retVal;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/// DbgInfoDwarfParser::DwarfLocationResolver
/// \brief Description: Helper function - Location resolver for DWARF Source->Brig->ISA, can resolve the Low level DwarfVariableLocation
/// \param[in]          hVarLoc - High level variable location
/// \param[in]          lAddr - low level address
/// \param[in]          lConsumer - low level consumer
/// \param[out]         o_lVarLocation - low level variable location
/// \param[in]          userData - user data - Default nullptr
/// \return             Success / failure.
/////////////////////////////////////////////////////////////////////////////////////////////////////
bool DbgInfoDwarfParser::DwarfLocationResolver(
    const DwarfVariableLocation& hVarLoc,
    const HwDbgUInt64& lAddr,
    const DbgInfoConsumerImpl<HwDbgUInt64, DwarfAddrType, DwarfVariableLocation>& lConsumer,
    DwarfVariableLocation& o_lVarLocation,
    void* userData)
{
    bool retVal = false;

    // Stack offset locations are resolved by evaluating the frame base location:
    if (DwarfVariableLocation::LOC_REG_STACK == hVarLoc.m_locationRegister)
    {
        o_lVarLocation = hVarLoc;
        retVal = true;
    }
    else
    {
        std::string lVarName;

        // This may be replaced with the actual BRIG statement table:
        HWDBG_ASSERT(userData != nullptr);

        /*
        if (userData != nullptr)
        {
            BrigVariable* pVarData = (BrigVariable*)(size_t)userData + (size_t)hVarLoc.m_location;
            lVarName = pVarData->name;
        }
        */

        VariableInfo<HwDbgUInt64, DwarfVariableLocation> lVarInfo;
        retVal = lConsumer.GetVariableInfoInCurrentScope(lAddr, lVarName, lVarInfo);

        if (retVal)
        {
            // [US] 21/8/12:
            // For non-AMD HSA implementations, this might require some changes
            // (the below code assumes the answers are "no")
            // 1. Can a HL variable be a LL constant?
            // 2. Can a HL register be a LL memory offset?

            // Add the data from the HL var location:
            o_lVarLocation = lVarInfo.m_varValue.m_varValueLocation;
            //            o_lVarLocation.m_loca = hVarLoc.m_locationType;
            o_lVarLocation.m_locationOffset += hVarLoc.m_locationOffset;

            if (ULLONG_MAX != hVarLoc.m_locationResource)
            {
                o_lVarLocation.m_locationResource = hVarLoc.m_locationResource;
            }

            if (UINT_MAX != hVarLoc.m_isaMemoryRegion)
            {
                o_lVarLocation.m_isaMemoryRegion = hVarLoc.m_isaMemoryRegion;
            }
        }
    }

    return retVal;
}

/// -----------------------------------------------------------------------------------------------
/// KernelBinary
/// \brief Description: Constructor
/// \param[in]          pBinaryData - buffer
/// \param[in]          binarySize - size
/// -----------------------------------------------------------------------------------------------
KernelBinary::KernelBinary(const void* pBinaryData, size_t binarySize) : m_pBinaryData(nullptr), m_binarySize(0)
{
    setBinary(pBinaryData, binarySize);
}

/// -----------------------------------------------------------------------------------------------
/// KernelBinary
/// \brief Description: Copy Constructor
/// \param[in]          rhs - other KernelBinary
/// -----------------------------------------------------------------------------------------------
KernelBinary::KernelBinary(const KernelBinary& rhs) : m_pBinaryData(nullptr), m_binarySize(0)
{
    setBinary(rhs.m_pBinaryData, rhs.m_binarySize);
}

/// -----------------------------------------------------------------------------------------------
/// ~KernelBinary
/// \brief Description: Releases the buffer
/// -----------------------------------------------------------------------------------------------
KernelBinary::~KernelBinary()
{
    setBinary(nullptr, 0);
}

/// -----------------------------------------------------------------------------------------------
/// operator=
/// \brief Description: Copy assignment operator
/// \param[in]          rhs - other KernelBinary
/// -----------------------------------------------------------------------------------------------
KernelBinary& KernelBinary::operator=(const KernelBinary& rhs)
{
    setBinary(rhs.m_pBinaryData, rhs.m_binarySize);

    return *this;
}

#ifdef HWDBGINFO_MOVE_SEMANTICS
/// -----------------------------------------------------------------------------------------------
/// KernelBinary
/// \brief Description: Move Constructor
/// \param[in]          xhs - other KernelBinary
/// -----------------------------------------------------------------------------------------------
KernelBinary::KernelBinary(KernelBinary&& xhs) : m_pBinaryData(xhs.m_pBinaryData), m_binarySize(xhs.m_binarySize)
{
    xhs.m_pBinaryData = nullptr;
    xhs.m_binarySize = 0;
}

/// -----------------------------------------------------------------------------------------------
/// operator=
/// \brief Description: Move assignment operator
/// \param[in]          xhs - other KernelBinary
/// -----------------------------------------------------------------------------------------------
KernelBinary& KernelBinary::operator=(KernelBinary&& xhs)
{
    m_pBinaryData = xhs.m_pBinaryData;
    m_binarySize = xhs.m_binarySize;
    xhs.m_pBinaryData = nullptr;
    xhs.m_binarySize = 0;

    return *this;
}
#endif

/// -----------------------------------------------------------------------------------------------
/// setBinary
/// \brief Description: Releases the previous buffer and copies the new one over:
/// \param[in]          pBinaryData
/// \param[in]          binarySize
/// -----------------------------------------------------------------------------------------------
void KernelBinary::setBinary(const void* pBinaryData, size_t binarySize)
{
    if (nullptr != m_pBinaryData)
    {
        delete[] (unsigned char*)m_pBinaryData;
        m_pBinaryData = nullptr;
        m_binarySize = 0;
    }

    m_binarySize = binarySize;

    if ((nullptr != pBinaryData) && (0 < m_binarySize))
    {
        unsigned char* pBuffer = new(std::nothrow) unsigned char[m_binarySize];
        if (nullptr != pBuffer)
        {
            ::memcpy(pBuffer, pBinaryData, m_binarySize);
            m_pBinaryData = (const void*)pBuffer;
        }
    }
}

/// -----------------------------------------------------------------------------------------------
/// isElf32Binary
/// \brief Description: Checks if the binary is a 32-bit elf (ELF32) format
/// \return bool success
/// -----------------------------------------------------------------------------------------------
bool KernelBinary::isElf32Binary() const
{
    bool retVal = false;

    // The ELF executable header is 16 bytes:
    if (m_binarySize > 16)
    {
        // Check the first 5 bytes of the file. In order, they should be:
        // 0x7f, 'E', 'L', 'F', ELFCLASS_32 = 0x01
        const unsigned char* pDataAsUBytes = (const unsigned char*)m_pBinaryData;
        retVal = ((0x7f == pDataAsUBytes[0]) &&
                  ('E'  == pDataAsUBytes[1]) &&
                  ('L'  == pDataAsUBytes[2]) &&
                  ('F'  == pDataAsUBytes[3]) &&
                  (0x01 == pDataAsUBytes[4]));
    }

    return retVal;
}

/// -----------------------------------------------------------------------------------------------
/// isElf64Binary
/// \brief Description: Checks if the binary is a 32-bit elf (ELF32) format
/// \return bool success
/// -----------------------------------------------------------------------------------------------
bool KernelBinary::isElf64Binary() const
{
    bool retVal = false;

    // The ELF executable header is 16 bytes:
    if (m_binarySize > 16)
    {
        // Check the first 5 bytes of the file. In order, they should be:
        // 0x7f, 'E', 'L', 'F', ELFCLASS_64 = 0x02
        const unsigned char* pDataAsUBytes = (const unsigned char*)m_pBinaryData;
        retVal = ((0x7f == pDataAsUBytes[0]) &&
                  ('E'  == pDataAsUBytes[1]) &&
                  ('L'  == pDataAsUBytes[2]) &&
                  ('F'  == pDataAsUBytes[3]) &&
                  (0x02 == pDataAsUBytes[4]));
    }

    return retVal;
}

/// -----------------------------------------------------------------------------------------------
/// getSubBufferAsBinary
/// \param[in]          offset
/// \param[in]          size
/// \param[out]         o_bufferAsBinary
/// \brief Description: Gets the sub buffer as a binary of itself:
/// \return bool success
/// -----------------------------------------------------------------------------------------------
bool KernelBinary::getSubBufferAsBinary(size_t offset, size_t size, KernelBinary& o_bufferAsBinary) const
{
    bool retVal = false;

    // Validate the values:
    if (offset + size <= m_binarySize)
    {
        // Copy the data:
        o_bufferAsBinary.setBinary((const void*)((size_t)m_pBinaryData + offset), size);
        retVal = true;
    }

    return retVal;
}

/// -----------------------------------------------------------------------------------------------
/// getTrimmedBufferAsBinary
/// \param[in]          start_trim
/// \param[in]          end_trim
/// \param[out]         o_bufferAsBinary
/// \brief Description: Gets the sub buffer obtained by removing the specified amount of bytes from
///                     the start and end, as a binary of itself:
/// \return bool success
/// -----------------------------------------------------------------------------------------------
bool KernelBinary::getTrimmedBufferAsBinary(size_t start_trim, size_t end_trim, KernelBinary& o_bufferAsBinary) const
{
    bool retVal = false;

    if (start_trim < m_binarySize && (start_trim + end_trim) < m_binarySize)
    {
        retVal = getSubBufferAsBinary(start_trim, m_binarySize - (start_trim + end_trim), o_bufferAsBinary);
    }

    return retVal;
}

/// -----------------------------------------------------------------------------------------------
/// getElfSectionAsBinary
/// \param[in]          sectionIndex
/// \param[out]         o_sectionAsBinary
/// \brief Description: Extract an ELF section as a binary itself
/// \return bool success
/// -----------------------------------------------------------------------------------------------
bool KernelBinary::getElfSectionAsBinary(int sectionIndex, KernelBinary& o_sectionAsBinary) const
{
    bool retVal = false;

    // Set the version of elf:
    elf_version(EV_CURRENT);

    // Initialize the binary as ELF from memory:
    Elf* pContainerElf = elf_memory((char*)m_pBinaryData, m_binarySize);

    if (nullptr != pContainerElf)
    {
        // Iterate the sections:
        Elf_Scn* pCurrentSection = nullptr;

        for (int i = 0; i < sectionIndex; i++)
        {
            pCurrentSection = elf_nextscn(pContainerElf, pCurrentSection);

            if (nullptr == pCurrentSection)
            {
                break;
            }
        }

        // If the section exists:
        if (nullptr != pCurrentSection)
        {
            // Get the section's data:
            Elf_Data* pSectionData = elf_getdata(pCurrentSection, nullptr);

            if (nullptr != pSectionData)
            {
                // Copy the values to the output parameter:
                o_sectionAsBinary.setBinary(pSectionData->d_buf, (size_t)pSectionData->d_size);

                // Report success:
                retVal = true;
            }
        }
    }

    return retVal;
}

/// -----------------------------------------------------------------------------------------------
/// getElfSectionAsBinary
/// \param[in]          sectionName
/// \param[out]         o_sectionAsBinary
/// \param[out]         o_pSectionLinkIndex
/// \brief Description: Extract an ELF section as a binary itself
/// \return bool success
/// -----------------------------------------------------------------------------------------------
bool KernelBinary::getElfSectionAsBinary(const std::string& sectionName, KernelBinary& o_sectionAsBinary, int* o_pSectionLinkIndex) const
{
    bool retVal = false;

    // Set the version of elf:
    elf_version(EV_CURRENT);

    // Initialize the binary as ELF from memory:
    Elf* pContainerElf = elf_memory((char*)m_pBinaryData, m_binarySize);

    if (nullptr != pContainerElf)
    {
        // Get the shared strings section:
        size_t sharedStringSectionIndex = (size_t)(-1);
        int rcShrstr = elf_getshdrstrndx(pContainerElf, &sharedStringSectionIndex);

        if ((0 == rcShrstr) && ((size_t)(-1) != sharedStringSectionIndex))
        {
            // Iterate the sections:
            Elf_Scn* pCurrentSection = elf_nextscn(pContainerElf, nullptr);

            while (nullptr != pCurrentSection)
            {
                // Only support ELF32 and ELF64:
                bool supportedBinaryFormat = false;
                size_t strOffset = 0;
                size_t shLink = 0;

                if (isElf32Binary())
                {
                    // Get the section header:
                    Elf32_Shdr* pCurrentSectionHeader = elf32_getshdr(pCurrentSection);

                    if (nullptr != pCurrentSectionHeader)
                    {
                        // Get the name and link if needed:
                        strOffset = pCurrentSectionHeader->sh_name;
                        shLink = pCurrentSectionHeader->sh_link;
                        supportedBinaryFormat = true;
                    }
                }
                else if (isElf64Binary())
                {
                    // Get the section header:
                    Elf64_Shdr* pCurrentSectionHeader = elf64_getshdr(pCurrentSection);

                    if (nullptr != pCurrentSectionHeader)
                    {
                        // Get the name and link if needed:
                        strOffset = pCurrentSectionHeader->sh_name;
                        shLink = pCurrentSectionHeader->sh_link;
                        supportedBinaryFormat = true;
                    }
                }

                if (supportedBinaryFormat)
                {
                    // Get the current section's name:
                    char* pCurrentSectionName = elf_strptr(pContainerElf, sharedStringSectionIndex, strOffset);

                    if (nullptr != pCurrentSectionName)
                    {
                        std::string currentSectionName;
                        currentSectionName = pCurrentSectionName;

                        if ((!currentSectionName.empty()) && (sectionName == currentSectionName))
                        {
                            // Get the section's data:
                            Elf_Data* pSectionData = elf_getdata(pCurrentSection, nullptr);

                            if (nullptr != pSectionData)
                            {
                                // Copy the values to the output parameter:
                                o_sectionAsBinary.setBinary(pSectionData->d_buf, (size_t)pSectionData->d_size);

                                // Return the link if requested:
                                if (nullptr != o_pSectionLinkIndex)
                                {
                                    *o_pSectionLinkIndex = (int)shLink;
                                }

                                // Report success:
                                retVal = true;
                                break;
                            }
                        }
                    }
                }

                // Get the next section:
                pCurrentSection = elf_nextscn(pContainerElf, pCurrentSection);
            }
        }
    }

    return retVal;
}

/// -----------------------------------------------------------------------------------------------
/// getElfSymbolAsBinary
/// \param[in]          symbol
/// \param[out]         o_symbolAsBinary
/// \brief Description: Extract an ELF symbol as a binary itself
/// \return bool success
/// -----------------------------------------------------------------------------------------------
bool KernelBinary::getElfSymbolAsBinary(const std::string& symbol, KernelBinary& o_symbolAsBinary) const
{
    bool retVal = false;

    // Set the version of elf:
    elf_version(EV_CURRENT);

    // Initialize the binary as ELF from memory:
    Elf* pContainerElf = elf_memory((char*)m_pBinaryData, m_binarySize);

    if (nullptr != pContainerElf)
    {
        // First get the symbol table section:
        KernelBinary symTabSection(nullptr, 0);
        int symbolStringTableIndex = -1;
        bool rcST = getElfSectionAsBinary(".symtab", symTabSection, &symbolStringTableIndex);

        if (rcST && (0 < symbolStringTableIndex))
        {
            if (!symbol.empty())
            {
                // Get the symbol data:
                int sectionIndex = -1;
                size_t offsetInSection = 0;
                size_t symbolSize = 0;

                if (isElf32Binary())
                {
                    int numberOfSymbols = (int)(symTabSection.m_binarySize / sizeof(Elf32_Sym));
                    Elf32_Sym* pCurrentSymbol = (Elf32_Sym*)symTabSection.m_pBinaryData;

                    for (int i = 0; i < numberOfSymbols; i++)
                    {
                        // Get the symbol name as a string:
                        char* pCurrentSymbolName = elf_strptr(pContainerElf, symbolStringTableIndex, pCurrentSymbol->st_name);

                        if (symbol == pCurrentSymbolName)
                        {
                            // Get the parameters:
                            sectionIndex = (int)pCurrentSymbol->st_shndx;
                            offsetInSection = (size_t)pCurrentSymbol->st_value;
                            symbolSize = (size_t)pCurrentSymbol->st_size;

                            // Stop searching:
                            break;
                        }

                        // Move the pointer ahead:
                        pCurrentSymbol++;
                    }
                }
                else if (isElf64Binary())
                {
                    int numberOfSymbols = (int)(symTabSection.m_binarySize / sizeof(Elf64_Sym));
                    Elf64_Sym* pCurrentSymbol = (Elf64_Sym*)symTabSection.m_pBinaryData;

                    for (int i = 0; i < numberOfSymbols; i++)
                    {
                        // Get the symbol name as a string:
                        char* pCurrentSymbolName = elf_strptr(pContainerElf, symbolStringTableIndex, pCurrentSymbol->st_name);

                        if (symbol == pCurrentSymbolName)
                        {
                            // Get the parameters:
                            sectionIndex = (int)pCurrentSymbol->st_shndx;
                            offsetInSection = (size_t)pCurrentSymbol->st_value;
                            symbolSize = (size_t)pCurrentSymbol->st_size;

                            // Stop searching:
                            break;
                        }

                        // Move the pointer ahead:
                        pCurrentSymbol++;
                    }
                }

                // Get the containing section:
                KernelBinary containingSection(nullptr, 0);
                bool rcSc = getElfSectionAsBinary(sectionIndex, containingSection);

                if (rcSc)
                {
                    // Get the data from it:
                    retVal = containingSection.getSubBufferAsBinary(offsetInSection, symbolSize, o_symbolAsBinary);
                }
            }
        }
    }

    return retVal;
}

/// -----------------------------------------------------------------------------------------------
/// listELFSectionNames
/// \param[out]         ov_sectionNames
/// \brief Description: Lists all the ELF section names
/// \return void
/// -----------------------------------------------------------------------------------------------
void KernelBinary::listELFSectionNames(std::vector<std::string>& o_sectionNames) const
{
    // Set the version of elf:
    elf_version(EV_CURRENT);

    // Initialize the binary as ELF from memory:
    Elf* pContainerElf = elf_memory((char*)m_pBinaryData, m_binarySize);

    if (nullptr != pContainerElf)
    {
        // Get the shared strings section:
        size_t sharedStringSectionIndex = (size_t)(-1);
        int rcShrstr = elf_getshdrstrndx(pContainerElf, &sharedStringSectionIndex);

        if ((0 == rcShrstr) && ((size_t)(-1) != sharedStringSectionIndex))
        {
            // Iterate the sections:
            Elf_Scn* pCurrentSection = elf_nextscn(pContainerElf, nullptr);

            while (nullptr != pCurrentSection)
            {
                // Only support ELF32 and ELF64:
                bool supportedBinaryFormat = false;
                size_t strOffset = 0;
                //size_t shLink = 0;

                if (isElf32Binary())
                {
                    // Get the section header:
                    Elf32_Shdr* pCurrentSectionHeader = elf32_getshdr(pCurrentSection);

                    if (nullptr != pCurrentSectionHeader)
                    {
                        // Get the name and link if needed:
                        strOffset = pCurrentSectionHeader->sh_name;
                        //shLink = pCurrentSectionHeader->sh_link;
                        supportedBinaryFormat = true;
                    }
                }
                else if (isElf64Binary())
                {
                    // Get the section header:
                    Elf64_Shdr* pCurrentSectionHeader = elf64_getshdr(pCurrentSection);

                    if (nullptr != pCurrentSectionHeader)
                    {
                        // Get the name and link if needed:
                        strOffset = pCurrentSectionHeader->sh_name;
                        //shLink = pCurrentSectionHeader->sh_link;
                        supportedBinaryFormat = true;
                    }
                }

                if (supportedBinaryFormat)
                {
                    // Get the current section's name:
                    char* pCurrentSectionName = elf_strptr(pContainerElf, sharedStringSectionIndex, strOffset);

                    if (nullptr != pCurrentSectionName)
                    {
                        // If it's not empty, add it to the vector:
                        std::string currentSectionName;
                        currentSectionName = pCurrentSectionName;

                        if (!currentSectionName.empty())
                        {
                            o_sectionNames.push_back(currentSectionName);
                        }
                    }
                }

                // Get the next section:
                pCurrentSection = elf_nextscn(pContainerElf, pCurrentSection);
            }
        }
    }
}

/// -----------------------------------------------------------------------------------------------
/// listELFSymbolNames
/// \param[out]         ov_symbolNames
/// \brief Description: Lists all the ELF symbol names
/// \return void
/// -----------------------------------------------------------------------------------------------
void KernelBinary::listELFSymbolNames(std::vector<std::string>& o_symbolNames) const
{
    // Set the version of elf:
    elf_version(EV_CURRENT);

    // Initialize the binary as ELF from memory:
    Elf* pContainerElf = elf_memory((char*)m_pBinaryData, m_binarySize);

    if (nullptr != pContainerElf)
    {
        // First get the symbol table section:
        KernelBinary symTabSection(nullptr, 0);
        int symbolStringTableIndex = -1;
        bool rcST = getElfSectionAsBinary(".symtab", symTabSection, &symbolStringTableIndex);

        if (rcST && (0 < symbolStringTableIndex))
        {
            // Get the symbol data:
            if (isElf32Binary())
            {
                int numberOfSymbols = (int)(symTabSection.m_binarySize / sizeof(Elf32_Sym));
                Elf32_Sym* pCurrentSymbol = (Elf32_Sym*)symTabSection.m_pBinaryData;

                for (int i = 0; i < numberOfSymbols; i++)
                {
                    // Get the symbol name as a string:
                    char* pCurrentSymbolName = elf_strptr(pContainerElf, symbolStringTableIndex, pCurrentSymbol->st_name);

                    if (nullptr != pCurrentSymbolName)
                    {
                        // If it's not empty, add it to the vector:
                        std::string currentSymbolName = pCurrentSymbolName;

                        if (!currentSymbolName.empty())
                        {
                            o_symbolNames.push_back(currentSymbolName);
                        }
                    }

                    // Move the pointer ahead:
                    pCurrentSymbol++;
                }
            }
            else if (isElf64Binary())
            {
                int numberOfSymbols = (int)(symTabSection.m_binarySize / sizeof(Elf64_Sym));
                Elf64_Sym* pCurrentSymbol = (Elf64_Sym*)symTabSection.m_pBinaryData;

                for (int i = 0; i < numberOfSymbols; i++)
                {
                    // Get the symbol name as a string:
                    char* pCurrentSymbolName = elf_strptr(pContainerElf, symbolStringTableIndex, pCurrentSymbol->st_name);

                    if (nullptr != pCurrentSymbolName)
                    {
                        // If it's not empty, add it to the vector:
                        std::string currentSymbolName = pCurrentSymbolName;

                        if (!currentSymbolName.empty())
                        {
                            o_symbolNames.push_back(currentSymbolName);
                        }
                    }

                    // Move the pointer ahead:
                    pCurrentSymbol++;
                }
            }
        }
    }
}
