


/*******************************************************************************
 * INTEL CORPORATION CONFIDENTIAL Copyright(c) 2017-2020 Intel Corporation. All Rights Reserved.
 *
 * The source code contained or described herein and all documents related to the
 * source code ("Material") are owned by Intel Corporation or its suppliers or
 * licensors. Title to the Material remains with Intel Corporation or its suppliers
 * and licensors. The Material contains trade secrets and proprietary and
 * confidential information of Intel or its suppliers and licensors. The Material
 * is protected by worldwide copyright and trade secret laws and treaty provisions.
 * No part of the Material may be used, copied, reproduced, modified, published,
 * uploaded, posted, transmitted, distributed, or disclosed in any way without
 * Intel's prior express written permission.
 *
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or delivery of
 * the Materials, either expressly, by implication, inducement, estoppel or
 * otherwise. Any license under such intellectual property rights must be express
 * and approved by Intel in writing.
 ********************************************************************************/

/**
 * @brief Header file defining types used in host umd->kmd interface.
 * @file nnpdrvTypes.h
 */

#pragma once

#include <stdint.h>
#include <unistd.h>
#include <limits.h>

typedef uint64_t NNPHostResource;    /**< handle to host resource        */

/**
 * @brief NNPDeviceInfo - static device info structure
 *
 * This structure describe static properties of a NNP-I device
 * which cannot change after the device is booted.
 */
typedef struct {
	uint32_t numIceDevices;		/**< number of ICE devices   */
	uint8_t driverVersionMajor;	/**< driver/runtime version major */
	uint8_t driverVersionMinor;	/**< driver/runtime version minor */
	uint8_t driverVersionDot;	/**< driver/runtime version dot   */
	uint8_t fwVersionMajor;		/**< ICE fw version major    */
	uint8_t fwVersionMinor;		/**< ICE fw version minor    */
	uint8_t fwVersionDot;		/**< ICE fw version dot      */
	uint64_t totalUnprotectedMemory;           /**< total device memory     */
	uint64_t totalEccMemory;	/**< total device ecc memory  */
	uint8_t stepping;	        /**< stepping number  */
} NNPDeviceInfo;

/**
 * @enum NNPDeviceBootState
 *
 * Describes the device boot state.
 */
typedef enum {
	NNP_BOOT_UNKNOWN,	/**< Card boot state is unknown                */
	NNP_BOOT_BIOS_READY,	/**< Card bios is up - OS boot did not started */
	NNP_BOOT_BOOT_STARTED,	/**< OS boot started                           */
	NNP_BOOT_DRIVER_READY,	/**< OS booted and device driver has loaded    */
	NNP_BOOT_DEVICE_READY,	/**< OS booted and device is ready to use      */
	NNP_BOOT_FAILED,	/**< OS boot failed - fatal error              */
	NNP_BOOT_RECOVERY_BIOS_READY,	/**< Card recovery bios is up          */
	NNP_BOOT_BIOS_UPDATE_STARTED, /**< Card bios update is in progress      */
	NNP_BOOT_MAX
} NNPDeviceBootState;

/**
 * @enum NNPDeviceFailureReason
 *
 * Describes the device boot state errors.
 */
typedef enum {
	NNP_FAILURE_NOT_FAILED,
	NNP_FAILURE_FAILED_VERSION,
	NNP_FAILURE_BOOT_FAILED,
	NNP_FAILURE_HOST_DRIVER_ERROR,
	NNP_FAILURE_KERNEL_CRASH,
	NNP_FAILURE_BIOS_UPDATE_REQUIRED,
	NNP_FAILURE_BIOS_UPDATE_FAILED,
	NNP_FAILURE_MAX
} NNPDeviceFailureReason;

/**
 * @brief NNPDeviceStatus - device state info structure
 *
 * This structure holds dynamic state of a NNP-I device.
 */
typedef struct {
	NNPDeviceBootState bootState;		/**< Device boot state			*/
	NNPDeviceFailureReason failReason;	/**< Device failure reason		*/
	uint32_t numActiveContexts;		/**< # of active inference contexts	*/
} NNPDeviceStatus;

/**
 * @brief NNPInferContextInfo - static device info structure
 *
 * This structure describe the properties of the infer context
 */
typedef struct {
	uint32_t deviceNum;  /**< NNP-I device number */
	uint32_t contextId;  /**< Device context ID        */
} NNPInferContextInfo;

/**
 * @enum NNPError
 *
 * Describe NNP-I host driver error codes
 */
typedef enum {
	NNP_NO_ERROR               = 0,  /**< No error */
	NNP_NO_SUCH_DEVICE         = 1,  /**< The specified device number does not exist */
	NNP_DEVICE_NOT_READY       = 2,  /**< The specified device is not active
					  *   either down or not yet booted.
					  */

	NNP_OUT_OF_MEMORY          = 3,  /**< There is not enough system memory
					  *   to complete the request
					  */
	NNP_NO_SUCH_RESOURCE       = 4,  /**< The specified resource handle does not exist */
	NNP_NO_SUCH_CONTEXT        = 5,  /**< The specified context handle does not exist */
	NNP_INCOMPATIBLE_RESOURCES = 6,  /**< The specified source and
					  *   destination resource handles are not
					  *   compatible to be copied from one to
					  *   the other.
					  */
	NNP_NO_SUCH_NETWORK        = 7,  /**< The specified device network handle does not exist */
	NNP_TOO_MANY_CONTEXTS      = 8,  /**< The limit of maximum contexts for a device has reached */
	NNP_INVALID_ARGUMENT       = 9,  /**< One of the function arguments is invalid */
	NNP_CONTEXT_BROKEN         = 10, /**< The infer context is in broken
					  *   state and must be either
					  *   recovered or destroyed
					  */
	NNP_DEVICE_ERROR           = 11, /**< The device is in a fatal state which requires h/w reset */
	NNP_TIMED_OUT              = 12, /**< Timeout given in the function argument was elapsed */
	NNP_BROKEN_MARKER          = 13, /**< A marker object was failed to be created on the device,
					  *   either because out-of-memory condition or internal driver error.
					  */
	NNP_IO_ERROR               = 14, /**< Data failed to be copied between the application and host driver. */
	NNP_NO_SUCH_COPY_HANDLE    = 15, /**< The specified copy handle does not exist */
	NNP_NO_SUCH_INFREQ_HANDLE  = 16, /**< The specified infer request handle does not exist */
	NNP_INTERNAL_DRIVER_ERROR  = 17, /**< An internal driver error has occurred! */
	NNP_OPERATION_INTERRUPTED  = 18, /**< The operation has interruptred before completion */
	NNP_NOT_SUPPORTED	   = 19,
	NNP_INVALID_EXECUTABLE_NETWORK_BINARY = 20,
	NNP_INFER_MISSING_RESOURCE = 21,
	NNP_HOSTRES_BROKEN         = 22,
	NNP_PERMISSION_DENIED      = 23,  /**< No permission */
	NNP_DEVICE_BUSY            = 24,  /**< Device is busy */
	NNP_INCOMPLETE_NETWORK     = 25,  /**< Network is incomplete - has no device resources */
	NNP_DEVNET_RESERVE_INSUFFICIENT_RESOURCES  = 26,  /**< Network reservatino failed no resources */
	NNP_OUT_OF_ECC_MEMORY      = 27,  /**< Failed to alloc device resource from ecc memory */
	NNP_NO_SUCH_CMDLIST        = 28,
	NNP_VERSIONS_MISMATCH      = 29,  /**< Kernel and user space versions are not match*/

	NNP_UNKNOWN_ERROR          = 999
} NNPError;

/**
 * @enum NNPCriticalError
 *
 * Describe NNP-I host driver critical error codes
 */
typedef enum {
	NNP_CRI_NO_ERROR		= 0,  /**< No critical error condifion */
	NNP_CRI_INTERNAL_DRIVER_ERROR	= 1,  /**< Internal driver error has occurred */
	NNP_CRI_NOT_SUPPORTED		= 2,  /**< Card s/w stack does not
					       *   support the scheduled infer
					       *   request
					       */
	NNP_CRI_GRACEFUL_DESTROY        = 3,  /**< No real critical error! A graceful
					       *   context destroy asked by the
					       *   administrator.
					       */
	NNP_CRI_CARD_RESET              = 4,  /**< Card has been reset */
	NNP_CRI_INFREQ_FAILED           = 5,  /**< Infer-req execution failed */
	NNP_CRI_INFREQ_NETWORK_RESET    = 6,  /**< Infer-req failed, network reset required */
	NNP_CRI_INFREQ_CARD_RESET       = 7,  /**< Infer-req failed, card reset required */
	NNP_CRI_INPUT_IS_DIRTY          = 8,
	NNP_CRI_FAILED_TO_RELEASE_CREDIT = 9,
	NNP_CRI_UNKNOWN_CRITICAL_ERROR	= 999 /**< Other critical error has occurred */
} NNPCriticalError;
