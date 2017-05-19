//==============================================================================
// Copyright (c) 2015-2016 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools
/// \file
/// \brief  This class manages the dynamic loading of HSA entry points hsa-runtime{32,64}.dll and libhsa-runtime{32,64}.so
//==============================================================================

#ifdef __linux
    #include <sys/utsname.h>
#endif

#include <string>
#include <cstring>
#include "HSAModule.h"

#if defined(_WIN64) || defined(__LP64__)
    #if defined(_WIN32) || defined(__CYGWIN__)
        const char* HSAModule::s_defaultModuleName = "hsa-runtime64.dll";
    #else // LINUX
        const char* HSAModule::s_defaultModuleName = "libhsa-runtime64.so.1";
    #endif
#else
    #pragma message("HSA Foundation runtime does not support 32-bit builds")
    #if defined(_WIN32) || defined(__CYGWIN__)
        const char* HSAModule::s_defaultModuleName = "hsa-runtime.dll";
    #else // LINUX
        const char* HSAModule::s_defaultModuleName = "libhsa-runtime.so.1";
    #endif
#endif

#define ROCM_1_2_AMD_VEN_LOADER_EXTENSION 3

HSAModule::HSAModule(void) : m_isModuleLoaded(false)
{
    Initialize();
    LoadModule();
}

HSAModule::~HSAModule(void)
{
    UnloadModule();
}

void HSAModule::Initialize()
{
#define X(SYM) SYM = nullptr;
    HSA_RUNTIME_API_TABLE;
    HSA_EXT_FINALIZE_API_TABLE;
    HSA_EXT_IMAGE_API_TABLE;
    HSA_EXT_AMD_API_TABLE;
    HSA_VEN_AMD_LOADER_API_TABLE;
    HSA_NON_INTERCEPTABLE_RUNTIME_API_TABLE;
#undef X

    m_isModuleLoaded = false;
    m_finalizeExtTableLoaded = false;
    m_imageExtTableLoaded = false;
    m_amdExtTableLoaded = false;
    m_amdVenLoaderTableLoaded = false;
    m_nonInterceptableTableLoaded = false;
}

void HSAModule::UnloadModule()
{
    m_dynamicLibraryHelper.UnloadModule();
    Initialize();
}

bool HSAModule::LoadModule(const std::string& moduleName)
{
#ifdef __linux
    utsname unameBuf;

    if (0 == uname(&unameBuf))
    {
        std::string strRelease(unameBuf.release);

        if (std::string::npos == strRelease.find("kfd-compute"))
        {
            return false;
        }
    }

#endif

    // Load from specified module
    bool bLoaded = m_dynamicLibraryHelper.LoadModule(moduleName);

    if (!bLoaded)
    {
        // Load from deafult module
        bLoaded = m_dynamicLibraryHelper.LoadModule(s_defaultModuleName);
    }

    if (bLoaded)
    {

#define MAKE_STRING(s) "hsa_"#s
#define X(SYM) SYM = reinterpret_cast<decltype(::hsa_##SYM)*>(m_dynamicLibraryHelper.GetProcAddress(MAKE_STRING(SYM)));
        HSA_RUNTIME_API_TABLE;
        HSA_EXT_AMD_API_TABLE;
        HSA_NON_INTERCEPTABLE_RUNTIME_API_TABLE;
#undef X
#undef MAKE_STRING

        // Check if we were able to initialize the various sets of function pointers
        // We will consider the module to have successfully loaded if:
        //   1) all core runtime functions are non-nullptr or
        //   2) all functions shared by ROCM 1.2 and ROCM 1.2 are non-null
        // All extension functions are considered optional for the m_isModuleLoaded flag
#define X(SYM) && SYM != nullptr
        bool isCommonTableLoaded = true COMMON_1_2_AND_1_3;
        bool isRuntimeTableLoaded = true HSA_RUNTIME_API_TABLE;
        m_amdExtTableLoaded = true HSA_EXT_AMD_API_TABLE;
        m_nonInterceptableTableLoaded = true HSA_NON_INTERCEPTABLE_RUNTIME_API_TABLE;
#undef X

        m_isModuleLoaded = isRuntimeTableLoaded;

#define X(SYM) || SYM != nullptr
        bool isAnyCommonFunctionLoaded = false COMMON_1_2_AND_1_3;
#undef X

        if (!m_isModuleLoaded && isAnyCommonFunctionLoaded)
        {
            m_isModuleLoaded = isCommonTableLoaded;
        }

        // initialize the extension functions
        if (m_isModuleLoaded)
        {
            bool extensionSupported = false;
            bool mustCallShutdown = false;
            hsa_status_t status = system_extension_supported(HSA_EXTENSION_FINALIZER, 1, 0, &extensionSupported);

            if (HSA_STATUS_ERROR_NOT_INITIALIZED == status)
            {
                // hsa runtime not initialized yet, initialize it now
                status = init();

                if (HSA_STATUS_SUCCESS == status)
                {
                    mustCallShutdown = true;
                    uint16_t finalizerMinorVersion = 0;
                    status = system_major_extension_supported(HSA_EXTENSION_FINALIZER, 1, &finalizerMinorVersion, &extensionSupported);
                }
                else
                {
                    m_isModuleLoaded = false;
                }
            }

            if (m_isModuleLoaded)
            {
                if ((HSA_STATUS_SUCCESS == status) && extensionSupported)
                {
                    hsa_ext_finalizer_1_00_pfn_t finalizerTable;
                    memset(&finalizerTable, 0, sizeof(hsa_ext_finalizer_1_00_pfn_t));
                    status = system_get_major_extension_table(HSA_EXTENSION_FINALIZER, 1, sizeof(hsa_ext_finalizer_1_00_pfn_t), &finalizerTable);

                    if (HSA_STATUS_SUCCESS == status)
                    {

#define X(SYM) SYM = finalizerTable.hsa_##SYM;
                        HSA_EXT_FINALIZE_API_TABLE;
                        m_finalizeExtTableLoaded = true;
#undef X
                    }
                }

                uint16_t minorVer = 0;
                status = system_major_extension_supported(HSA_EXTENSION_IMAGES, 1, &minorVer, &extensionSupported);

                if ((HSA_STATUS_SUCCESS == status) && extensionSupported)
                {
                    hsa_ext_images_1_pfn_t imagesTable;
                    memset(&imagesTable, 0, sizeof(hsa_ext_images_1_pfn_t));
                    status = system_get_major_extension_table(HSA_EXTENSION_IMAGES, 1, sizeof(hsa_ext_images_1_pfn_t), &imagesTable);

                    if (HSA_STATUS_SUCCESS == status)
                    {

#define X(SYM) SYM = imagesTable.hsa_##SYM;
                        HSA_EXT_IMAGE_API_TABLE;
                        m_imageExtTableLoaded = true;
#undef X
                    }
                }

                uint16_t amdLoaderExtension = HSA_EXTENSION_AMD_LOADER;
                uint16_t amdLoaderMinorVersion;
                status = system_major_extension_supported(amdLoaderExtension, 1, &amdLoaderMinorVersion, &extensionSupported);

                if ((HSA_STATUS_SUCCESS != status) || !extensionSupported)
                {
                    amdLoaderExtension = ROCM_1_2_AMD_VEN_LOADER_EXTENSION;
                    status = system_major_extension_supported(amdLoaderExtension, 1, &amdLoaderMinorVersion, &extensionSupported);
                }

                if ((HSA_STATUS_SUCCESS == status) && extensionSupported)
                {
                    hsa_ven_amd_loader_1_00_pfn_t loaderTable;
                    memset(&loaderTable, 0, sizeof(hsa_ven_amd_loader_1_00_pfn_t));
                    status = system_get_major_extension_table(amdLoaderExtension, 1, sizeof(hsa_ven_amd_loader_1_00_pfn_t), &loaderTable);

                    if (HSA_STATUS_SUCCESS == status)
                    {
#define X(SYM) SYM = loaderTable.hsa_##SYM;
                        HSA_VEN_AMD_LOADER_API_TABLE;
                        m_amdVenLoaderTableLoaded = true;
#undef X
                    }
                }

                if (mustCallShutdown)
                {
                    // if we initialzed the runtime, then shut it down now
                    shut_down();
                }
            }
        }
    }

    return m_isModuleLoaded;
}

