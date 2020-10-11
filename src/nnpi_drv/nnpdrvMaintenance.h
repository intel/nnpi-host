/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

/**
 * @brief Header file defining umd->kmd maintenance interface
 * @file nnpdrvMaintenance.h
 */

#pragma once

#include "nnpdrvTypes.h"
#include <sys/types.h>
#include "nnpiml_types.h"
#include "nnpiml_events.h"
#include "nnp_hwtrace_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAINT_PCIE_INJECT_NONE              0
#define MAINT_PCIE_INJECT_CORR              1
#define MAINT_PCIE_INJECT_UNCORR            2
#define MAINT_PCIE_INJECT_UNCORR_FATAL      3

/**
 * @brief Disable a device
 *
 * This function marks the nnpi device specified by device number as disabled,
 * No new inference contexts will be created on the device until it will get
 * enabled again.
 * If abort is non-zero, then all current inference contexts will be out in
 * broken state in order to inform applications that a graceful context destroy
 * is requested, applications may recover the context and continue as usual with
 * no data loss or re-schedules, or it may be decide to destroy the context.
 * Also, when abort is non-zero the timeout_us specifies a timeout value to wait
 * for all contexts of the device to get destroyed. a value of UINT32_MAX can be
 * used to specify no timeout wait.
 *
 * @param[in]  device_number     Device number
 *
 * @retval NNP_NO_ERROR          Success
 * @retval NNP_NO_SUCH_DEVICE    Device does not exist
 * @retval NNP_TIMED_OUT         abort is non-zero and either timeout expired
 *                               or function interrupted before all contexts got
 *                               destroyed.
 * @retval NNP_UNKNOWN_ERROR     Internal driver error
 */
NNPError nnpmntDisableDevice(uint32_t device_number,
			     uint32_t abort,
			     uint32_t timeout_us);

/**
 * @brief enable a device
 *
 * Enables a device that was previously disabled by nnpmntDisableDevice
 */
NNPError nnpmntEnableDevice(uint32_t device_number);

/**
 * @brief retrieve list of pids that has an inference context active on a device
 */
NNPError nnpmntGetDeviceInferProcessIDs(uint32_t device_number,
					pid_t    *out_pids,
					uint32_t  out_pids_size,
					uint32_t *out_num_pids);

/**
 * @brief reset a device
 */
NNPError nnpmntResetDevice(uint32_t device_number,
			   uint32_t force,
			   const char *image_name);
/*
 * @brief retrieve max device number
 */
NNPError nnpmntGetMaxDeviceNumber(uint32_t *out_max_device_number);

/**
 * @brief retrieve device states
 */
NNPError nnpmntGetDeviceStatus(uint32_t device_number, nnpimlDeviceStatus *out_status);

/**
 * @brief create event report client
 */
NNPError nnpmntCreateDeviceEvent(uint64_t device_mask, int *client_handle);
NNPError nnpmntDestroyDeviceEvent(int client_handle);

/**
 * @brief event report client, read events from driver
 */
NNPError nnpmntReadDeviceEvents(int client_handle,
				uint32_t timeout_us,
				nnpimlDeviceEvent *out_events,
				uint32_t max_events,
				uint32_t *out_num_events);

/**
 * @brief set event warning threshold
 */
NNPError nnpmntSetWarningThreshold(uint32_t device_number, nnpimlEventThresholdType type, uint32_t value);

/**
 * @brief get event warning threshold
 */
NNPError nnpmntGetWarningThreshold(uint32_t device_number, nnpimlEventThresholdType type, uint32_t *value);

NNPError nnpmntGetCrashLog(uint32_t device_number,
			   void    *out_buf,
			   uint32_t out_buf_size,
			   uint32_t *actual_size);

NNPError nnpmntInjectError(uint32_t device_number,
			   uint32_t error_type);

/**
 * @brief This function initialize tracing agent.
 *
 * The following errors may be returned:
 *      NNP_IO_ERROR         - internal driver error
 *      NNP_UNKNOWN_ERROR    - internal driver error
 *
 * @param[in]  device_number     Device number
 *
 * @retval NNP_NO_ERROR          Success
 * @retval NNP_IO_ERROR          Internal driver error
 * @retval NNP_UNKNOWN_ERROR     Internal driver error
 */
NNPHwTraceError nnpmntTracingAgentInit(uint32_t device_number);

/**
 * @brief This function stops and cleanup tracing agent.
 *
 * The following errors may be returned:
 *      NNP_IO_ERROR         - internal driver error
 *      NNP_UNKNOWN_ERROR    - internal driver error
 *
 * @param[in]  device_number     Device number
 *
 * @retval NNP_NO_ERROR          Success
 * @retval NNP_IO_ERROR          Internal driver error
 * @retval NNP_UNKNOWN_ERROR     Internal driver error
 */
NNPHwTraceError nnpmntTracingAgentFini(uint32_t device_number);



/**
 * @brief This function set dtf stream resources with 2 diffrent host resources
 * with the same size, if one of the input resources is null it will notify
 * driver to cleanup stream resources.
 *
 * The following errors may be returned:
 *      NNP_IO_ERROR         - internal driver error
 *      NNP_UNKNOWN_ERROR    - internal driver error
 *
 * @param[in]  device_number     Device number
 * @param[in]  hostResource      host resource array
 * @param[in]  hostRes_count     host resources array size
 *
 * @retval NNP_NO_ERROR          Success
 * @retval NNP_IO_ERROR          Internal driver error
 * @retval NNP_UNKNOWN_ERROR     Internal driver error
 */
NNPHwTraceError nnpmntSetTracingStreamResources(uint32_t device_number,
						NNPHostResource *hostResource,
						uint32_t hostRes_count);


/**
 * @brief This function return the next buffer index with byte_to_copy from Buffer - buffer contain trace data from NPK
 *
 * The following errors may be returned:
 *      NNP_IO_ERROR         - internal driver error
 *      NNP_UNKNOWN_ERROR    - internal driver error
 *
 * @param[in]   device_number    Device number
 * @param[out]  resource_index   resource index - match to how resources were given on set tracing resources
 * @param[out]  bytes_to_copy    how many bytes were written to resource.
 *
 * @retval NNP_NO_ERROR          Success
 * @retval NNP_IO_ERROR          Internal driver error
 * @retval NNP_UNKNOWN_ERROR     Internal driver error
*/
NNPHwTraceError nnpmntGetNextTracingResource(uint32_t  device_number,
					     uint32_t *resource_index,
					     uint32_t *bytes_to_copy,
					     uint8_t  *bIsLast);


/**
 * @brief This function release resource once user done proccesing it - notify driver it can reuse this resource.
 *
 * The following errors may be returned:
 *      NNP_IO_ERROR         - internal driver error
 *      NNP_UNKNOWN_ERROR    - internal driver error
 *
 * @param[in]  device_number     Device number
 * @param[in]  resource_index	 index for current resource - as given in set tracing resrouce.
 *
 * @retval NNP_NO_ERROR          Success
 * @retval NNP_IO_ERROR          Internal driver error
 * @retval NNP_UNKNOWN_ERROR     Internal driver error
*/
NNPHwTraceError nnpmntTracingUnlockResource(uint32_t device_number,
					    uint32_t resource_index);

/**
 * @brief This function queries trace status of a given device.
 *
 * The following errors may be returned:
 *      NNP_IO_ERROR         - internal driver error
 *      NNP_UNKNOWN_ERROR    - internal driver error
 *
 * @param[in]  device_number            Device number
 * @param[out] status			status of hwtrace device.
 * @param[out] host_resource_count	number of allocated resources on card.
 * @param[out] host_max_resource_size   max available resource size.
 *
 * @retval NNP_NO_ERROR          Success
 * @retval NNP_IO_ERROR          Internal driver error
 * @retval NNP_UNKNOWN_ERROR     Internal driver error
*/
NNPHwTraceError nnpmntTracingGetStatus(uint32_t device_number,
				       NNPHwTraceDeviceStatus *status,
				       uint32_t *host_resource_count,
				       uint32_t *max_resource_size);

#ifdef __cplusplus
} // of extern "C"
#endif
