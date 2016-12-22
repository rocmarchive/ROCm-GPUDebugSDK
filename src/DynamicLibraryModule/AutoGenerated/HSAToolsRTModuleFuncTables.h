//==============================================================================
// Copyright (c) 2015-2016 Advanced Micro Devices, Inc. All rights reserved.
/// \author AMD Developer Tools Team
/// \file
/// \brief THIS CODE WAS AUTOGENERATED BY PASSTHROUGHGENERATOR ON 08/18/16
//==============================================================================

#ifndef _HSATOOLSRTMODULEFUNCTABLES_H_
#define _HSATOOLSRTMODULEFUNCTABLES_H_

#define HSA_TOOLS_INTERFACES_API_TABLE \
    X(ext_tools_set_callback_functions) \
    X(ext_tools_get_callback_functions) \
    X(ext_tools_set_callback_arguments) \
    X(ext_tools_get_callback_arguments) \

#define HSA_TOOLS_DEBUGGER_API_TABLE \
    X(ext_tools_set_correlation_handler) \
    X(ext_tools_wave_control) \
    X(ext_tools_flush_cache) \
    X(ext_tools_install_trap) \
    X(ext_tools_set_exception_policy) \
    X(ext_tools_get_exception_policy) \
    X(ext_tools_set_kernel_execution_mode) \
    X(ext_tools_get_kernel_execution_mode) \
    X(ext_tools_register) \
    X(ext_tools_unregister) \
    X(ext_tools_address_watch) \
    X(ext_tools_get_dispatch_debug_info) \
    X(ext_tools_dmacopy) \
    X(ext_tools_create_event) \
    X(ext_tools_wait_event) \
    X(ext_tools_destroy_event) \

#define HSA_TOOLS_PROFILER_API_TABLE \
    X(ext_tools_create_pmu) \
    X(ext_tools_release_pmu) \
    X(ext_tools_get_counter_block_by_id) \
    X(ext_tools_get_all_counter_blocks) \
    X(ext_tools_get_pmu_state) \
    X(ext_tools_pmu_begin) \
    X(ext_tools_pmu_end) \
    X(ext_tools_pmu_wait_for_completion) \
    X(ext_tools_set_pmu_parameter) \
    X(ext_tools_get_pmu_parameter) \
    X(ext_tools_get_pmu_info) \
    X(ext_tools_create_counter) \
    X(ext_tools_destroy_counter) \
    X(ext_tools_destroy_all_counters) \
    X(ext_tools_get_enabled_counters) \
    X(ext_tools_get_all_counters) \
    X(ext_tools_set_counter_block_parameter) \
    X(ext_tools_get_counter_block_parameter) \
    X(ext_tools_get_counter_block_info) \
    X(ext_tools_get_counter_block) \
    X(ext_tools_set_counter_enabled) \
    X(ext_tools_is_counter_enabled) \
    X(ext_tools_is_counter_result_ready) \
    X(ext_tools_get_counter_result) \
    X(ext_tools_set_counter_parameter) \
    X(ext_tools_get_counter_parameter) \
    X(ext_tools_register_aql_trace_callback) \
    X(ext_tools_queue_create_profiled) \
    X(ext_tools_get_kernel_times) \



#endif // _HSATOOLSRTMODULEFUNCTABLES_H_
