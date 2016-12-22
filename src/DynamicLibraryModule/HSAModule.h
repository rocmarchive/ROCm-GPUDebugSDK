//==============================================================================
// Copyright (c) 2015-2016 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools
/// \file
/// \brief  This class manages the dynamic loading of HSA entry points hsa-runtime{32,64}.dll and libhsa-runtime{32,64}.so
//==============================================================================

#ifndef _HSA_MODULE_H_
#define _HSA_MODULE_H_

#include "DynamicLibraryModule.h"
#include "AutoGenerated/HSARTModuleDecls.h"
#include "AutoGenerated/HSARTModuleFuncTables.h"

/// This class handles the dynamic loading of hsa-runtime.dll/libhsa-runtime.so.
/// \note There will typically be one of these objects.
///       That instance will be global.
///       There is a trap for the unwary.
///       The order of global ctors is only defined within a single compilation unit.
///       So, one should not use these interfaces before "main" is reached.
///       This is different than calling these functions when the .dll/.so is linked against.
class HSAModule
{
public:

    /// Default name to use for construction.
    /// This is usually hsa-runtime.dll or libhsa-runtime.so.
    static const char* s_defaultModuleName;

    /// Constructor
    HSAModule();

    /// destructor
    ~HSAModule();

    /// Load module.
    /// \param[in] name The module name.
    /// \return         true if successful, false otherwise
    bool LoadModule(const std::string& name = s_defaultModuleName);

    /// Unload the HSA runtime module.
    void UnloadModule();

    /// Indicates whether the HSA runtime module has been loaded and all the expected entry points are valid
    /// \returns enumeration value to answer query.
    bool IsModuleLoaded() { return m_isModuleLoaded; }


#define X(SYM) decltype(::hsa_##SYM)* SYM;
    HSA_RUNTIME_API_TABLE;
    HSA_EXT_FINALIZE_API_TABLE;
    HSA_EXT_IMAGE_API_TABLE;
    HSA_EXT_AMD_API_TABLE;
    HSA_VEN_AMD_LOADER_API_TABLE;
    HSA_NON_INTERCEPTABLE_RUNTIME_API_TABLE;
#undef X

private:
    /// Initialize the internal data
    void Initialize();

    bool                 m_isModuleLoaded;       ///< Flag indicating whether the HSA runtime module has been loaded and all the expected entry points are valid
    DynamicLibraryModule m_dynamicLibraryHelper; ///< Helper to load/initialize the runtime entry points
};

#endif