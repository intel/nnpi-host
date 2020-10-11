/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

/**
 * @file nnpiml_events.h
 *
 * @brief Header file defining nnpiml device events control and reporting
 * interface functions.
 *
 * This header file defines the interface functions used for retrieving nnpi
 * device event reporting. Those are mainly error or warning conditions, like
 * ecc errors, nnpi device crash, thermal or power events.
 */

#pragma once

#include "nnpiml_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t nnpimlEventPollHandle; /**< Handle to nnpi event poll object */

/**
 * @brief Creates device event pollable handle.
 *
 * The function creates a handle through which device error and
 * warning events can be communicated to the calling application.
 *
 * device_mask is a bitmask specifying which nnpi devices in the system
 * should be included. Bit 0 (LSB) corresponds to device 0, Bit 1 to device 1
 * and so on. Only devices that exist at the time of this call will be
 * considered. The created event poll object will not include devices that are
 * part of the device mask but was not exist at the time of the event poll
 * creation.
 *
 * When warning or error events occur on one of the nnpi devices specified in
 * the device_mask parameter, it can be read using the nnpimlReadDeviceEvents
 * function.
 *
 * When the handle is no longer needed, it should be closed using the
 * nnpimlDestroyDeviceEventPoll function.
 *
 * @param[in]  device_mask   Mask to select nnpi devices
 * @param[out] out_handle    Returned handle to the event poll object
 *
 * @retval NNPIML_SUCCESS           Success
 * @retval NNPIML_INVALID_ARGUMENT  out_handle must not be NULL
 */
nnpimlStatus nnpimlCreateDeviceEventPoll(uint64_t               device_mask,
					 nnpimlEventPollHandle *out_handle);

/**
 * @brief Destroys device event pollable handle.
 *
 * The function destroys the handle previously created by a call to
 * nnpimlCreateDeviceEventPoll.
 *
 * @param[in] handle    Handle to the event poll object
 *
 * @retval NNPIML_SUCCESS           Success
 * @retval NNPIML_INVALID_ARGUMENT  handle is not a valid handle
 */
nnpimlStatus nnpimlDestroyDeviceEventPoll(nnpimlEventPollHandle handle);

/**
 * @brief Read device error and warning events
 *
 * This function reads any device error or warning events from a pollable
 * handle created by the nnpimlCreateDeviceEventPoll function call.
 *
 * The function will block and wait until some error or warning event is
 * signaled on one of the nnpi devices specified in the device_mask parameter
 * when the handle was created.
 *
 * timeout_us is a timeout value in microseconds, a value of zero will cause
 * the function to return without any blocking which allows the application to
 * poll for any error or warining event. A value of UINT32_MAX will cause
 * the function to wait until an error or warning event exist without any
 * timeout.
 *
 * When the function succeed it returns up to 'max_events' device events into
 * the device events array specified by the out_events parameter.
 * Each error event entry in the array corresponds to a specific device, there
 * may be multiple events for the same device.
 *
 * @param[in]  handle         Handle returned from nnpimlCreateDeviceEventPoll
 * @param[in]  timeout_us     Timeout interval, in micro-seconds
 * @param[out] out_events     Returned array of nnpi device events
 * @param[in]  max_events     Maximum events to be copied into out_events array
 * @param[out] out_num_events Returns the actual number of returned events
 *
 * @retval NNPIML_SUCCESS           Success
 * @retval NNPIML_INVALID_ARGUMENT  Some output parameter pointer is NULL or
 *                                  handle is invalid
 * @retval NNPIML_TIMED_OUT         Timeout elapsed but events are not
 *                                  available
 * @retval NNPIML_INTERRUPTED       device operation interrupted
 */
nnpimlStatus nnpimlReadDeviceEvents(nnpimlEventPollHandle handle,
				    uint32_t              timeout_us,
				    nnpimlDeviceEvent    *out_events,
				    uint32_t              max_events,
				    uint32_t             *out_num_events);

/**
 * @brief set threshold for generating reported device events
 *
 * Some error and event conditions may generate a "device event" which is
 * reported to management applications through device event file descriptor
 * (see nnpimlCreateDeviceEventPoll).
 * Some error or event conditions may be configured to generate a reported
 * device event only when some threshold is reached. This function is used to
 * set those thresholds. The threshold value meaning depends on the specified
 * event threshold type, the following describe the threshold value for each
 * possible threshold type:
 *
 * type=NNPIML_EVENT_THRESHOLD_CORRECTED_ECC:
 *       value is number of corrected ECC errors that must happen before
 *       generating a reported device event. Default is 1.
 *
 * type=NNPIML_EVENT_THRESHOLD_UNCORRECTED_ECC
 *       value is number of uncorrected ECC errors that must happen before
 *       generating a reported device event. Default is 1.
 *
 * @param[in]  device_number   Device number
 * @param[in]  type            Event threshold type
 * @param[in]  value           Event threshold value
 *
 * @retval NNPIML_SUCCESS           Success
 * @retval NNPIML_NO_SUCH_DEVICE    Device does not exist
 * @retval NNPIML_PERMISSION_DENIED The user is not privileged
 */
nnpimlStatus nnpimlSetWarningThreshold(uint32_t                 device_number,
				       nnpimlEventThresholdType type,
				       uint32_t                 value);

/**
 * @brief retrieve current threshold value for generating device event.
 *
 * This function is used to retrieve the current threshold value set for
 * a specific nnpi device and event type. The threshod value is returned in
 * out_value pointer argument. See nnpimlSetWarningThreshold for more info
 * about the value for each threshold type.
 *
 * @param[in]  device_number   Device number
 * @param[in]  type            Event threshold type
 * @param[out] out_value       Event threshold value
 *
 * @retval NNPIML_SUCCESS          Success
 * @retval NNPIML_NO_SUCH_DEVICE   Device does not exist
 */
nnpimlStatus nnpimlGetWarningThreshold(uint32_t                 device_number,
				       nnpimlEventThresholdType type,
				       uint32_t                *out_value);

#ifdef __cplusplus
}
#endif
