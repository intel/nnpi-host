/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

/**
 * @file nnpiml_types.h
 *
 * @brief Header file defining nnpiml types.
 *
 * This header file defines common types used in the nnpiml interface library.
 */

#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NNPI_DEVICE_NAME_LEN         32  /**< Maximum length of device pci name string */
#define NNPI_BIOS_VERSION_LEN        72  /**< Maximum length of device bios version string */
#define NNPI_BOOT_IMAGE_VERSION_LEN  128 /**< Maximum length of device boot image version string */
#define NNPI_BOARD_NAME_LEN          64  /**< Maximum length of device board name */
#define NNPI_SERIAL_NUM_LEN          16  /**< Maximum length of device serial number string */
#define NNPI_PART_NUM_LEN            12  /**< Maximum length of device part number string */
#define NNPI_NAME_MAX                255 /**< Maximum length of string name*/

/**
 * @brief Return status from all nnpiml function calls
 */
typedef enum {
	NNPIML_SUCCESS                =  0,  /**< Function executed successfully */
	NNPIML_NO_COUNTER_SERVICES    =  1,  /**< No queriable counters exist */
	NNPIML_INVALID_ARGUMENT       =  2,  /**< Invalid function argument */
	NNPIML_NO_SUCH_QUERY_CONTEXT  =  3,  /**< Query context handle does not exist */
	NNPIML_CREATE_REPORT_FAILED   =  4,  /**< Creation of query report object has failed */
	NNPIML_NO_SUCH_COUNTER_REPORT =  5, /**< Query report handle does not exist */
	NNPIML_SAMPLE_FAILED          =  6, /**< Failed to sample a report object */
	NNPIML_REPORT_REFRESHED       =  7, /**< Report object has refreshed */
	NNPIML_NO_SUCH_DEVICE         =  8, /**< Specified device number does not exist */
	NNPIML_DEVICE_ERROR           =  9, /**< The specified device is not ready */
	NNPIML_PERMISSION_DENIED      = 10, /**< The caller is not privileged */
	NNPIML_TIMED_OUT              = 11, /**< Timeout has elapsed */
	NNPIML_BUF_TOO_SMALL          = 12, /**< User supplied buffer is too small */
	NNPIML_BOOT_IMAGE_NOT_FOUND   = 13, /**< Boot image file or name does not exist */
	NNPIML_BIOS_IMAGE_NOT_FOUND   = 14, /**< Bios image file or name does not exist */
	NNPIML_TRACE_ALREADY_INITED   = 15, /**< Trace was already initialized for given device */
	NNPIML_TRACE_NOT_INIT         = 16, /**< nnpimlTrace wasn't initialized */
	NNPIML_NO_TRACE_SERVICES      = 17, /**< No traceable events exist */
	NNPIML_NO_SUCH_TRACE_CONTEXT  = 18, /**< Trace context handle does not exist */
	NNPIML_END_OF_STREAM          = 19, /**< Stream has no more data to provide and should be closed */
	NNPIML_INTERRUPTED            = 20, /**< A blocking operation has interrupted */
	NNPIML_DEVICE_BUSY            = 21, /**< Device is busy */
	NNPIML_UNKNOWN_ERROR          = 22, /**< Unexpected error occurred */
	NNPIML_XFER_CRC_ERROR         = 23, /**< Data transfer to/from host failed on CRC error */
	NNPIML_NOT_AVAILABLE          = 24, /**< requested data is not available */
	NNPIML_INACTIVE_ICE           = 25, /**< requested data for inactive ice */
	NNPIML_NOT_SUPPORTED          = 26, /**< requested data not supported */
	NNPIML_IO_ERROR               = 27, /**< I/O error */
	NNPIML_BIOS_IMAGE_ALREADY_EXIST = 28, /**< Bios image cannot be installed, already exist */
	NNPIML_BIOS_IMAGE_INVALID_FORMAT = 29, /**< Bios image file format is not supported */
	NNPIML_NOT_ENOUGH_MEMORY         = 30, /**< There's not enough memory to complete the operation */
	NNPIML_TRACE_EVENT_NOT_EXIST     = 31,  /**< Given event name does not exist */
	NNPIML_TRACE_START_FAILED	 = 32  /**< Trace start failed */
} nnpimlStatus;

/**
 * @brief Get nnpiml status code description.
 *
 * nnpimlStatus describe all possible status codes of API.
 * This function will translate each one by returning a description string.
 *
 * @param[in]   status code
 *
 * @return      status description string
 */
const char *nnpimlStatusDescription(nnpimlStatus status);

/**
 * @enum nnpimlDeviceBootState
 *
 * Describes the device boot state.
 */
typedef enum {
	NNPIML_BOOT_STATE_UNKNOWN,      /**< Card boot state is unknown */
	NNPIML_BOOT_STATE_BIOS_READY,   /**< Card bios is up - OS boot did not yet started */
	NNPIML_BOOT_STATE_BOOT_STARTED, /**< OS boot started */
	NNPIML_BOOT_STATE_DRIVER_READY, /**< OS booted and device driver has loaded */
	NNPIML_BOOT_STATE_DEVICE_READY, /**< OS booted and device is ready to use */
	NNPIML_BOOT_STATE_FAILED,       /**< OS boot failed - fatal error */
	NNPIML_BOOT_STATE_RECOVERY_BIOS_READY,   /**< Card recovery bios is up - Bios update is required */
	NNPIML_BOOT_STATE_BIOS_UPDATE_STARTED /**< Bios update has started */
} nnpimlDeviceBootState;

/**
 * @enum nnpimlDeviceState
 *
 * Describes the device state.
 */
typedef enum {
	NNPIML_DEVICE_STATE_UNKNOWN,           /**< Card state is unknown */
	NNPIML_DEVICE_STATE_ACTIVE,            /**< Card is operate and ready */
	NNPIML_DEVICE_STATE_DISABLED,          /**< Card disabled, applications still running */
	NNPIML_DEVICE_STATE_DISABLED_AND_IDLE, /**< Card disabled, no inference applications are running */
	NNPIML_DEVICE_STATE_FAILED             /**< Card is in a failed state */
} nnpimlDeviceState;

/**
 * @enum nnpimlDeviceFailReason
 *
 * Describes a device failure condition
 */
typedef enum {
	NNPIML_FAILURE_NOT_FAILED,        /**< Device is not in a failed state */
	NNPIML_FAILURE_FAILED_VERSION,    /**< Device failed due to mismatch host driver version */
	NNPIML_FAILURE_BOOT_FAILED,       /**< Device failed to boot */
	NNPIML_FAILURE_HOST_DRIVER_ERROR, /**< Host driver failed to initialize the device */
	NNPIML_FAILURE_KERNEL_CRASH,      /**< Device OS has crashed */
	NNPIML_FAILURE_PCI_ERROR,         /**< Uncorrectable PCI error detected on the device */
	NNPIML_FAILURE_RESET_IN_PROGRESS, /**< Device reset has initiated uppon request */
	NNPIML_FAILURE_FATAL_MCE_ERROR,    /**< Device has fatal unrecoverable machine-check error */
	NNPIML_FAILURE_FATAL_DRAM_ECC_ERROR,   /**< Device has fatal unrecoverable DRAM ECC error */
	NNPIML_FAILURE_FATAL_ICE_EXEC_ERROR,  /**< Fatal ICE execution error requires device reset */
	NNPIML_FAILURE_CARD_HANG,          /**< Device is not responding - Hang detected */
	NNPIML_FAILURE_BIOS_UPDATE_REQUIRED, /**< Device Bios corrupted, requires bios update */
	NNPIML_FAILURE_BIOS_UPDATE_FAILED,    /**< Device bios update flow has failed */
	NNPIML_FAILURE_FATAL_DMA_ERROR /**< Device DMA engine hang */
} nnpimlDeviceFailReason;

typedef enum {
	POWER_SAVE_MODE,        /**< Power save mode */
	POWER_BALANCED,         /**< Power is balanced. This is the default mode */
	POWER_MAX_PERFORMANCE,  /**< Power is set for maximum performance */
	POWER_MAX
} nnpimlPowerSaveMode;


/**
 * @struct nnpimlDeviceStatus
 *
 * Describes current device status
 */
typedef struct {
	uint32_t               pciBus;         /**< pci bus number */
	uint32_t               pciSlot;        /**< pci slot number */
	char                   deviceName[NNPI_DEVICE_NAME_LEN]; /**< Device pci name string */
	nnpimlDeviceState      state;          /**< Device operation state */
	nnpimlDeviceBootState  bootState;      /**< Device boot state */
	nnpimlDeviceFailReason failReason;     /**< Device failure reason */
	uint32_t               biosPostCode;   /**< Code indicating bios boot stage */
	uint32_t               biosFlashProgress; /**< bios flash percentage - only during bios flash phase */
	uint32_t               numIceDevices;  /**< number of ICE devices   */
	char                   biosVersion[NNPI_BIOS_VERSION_LEN]; /**< Device bios version string */
	char                   imageVersion[NNPI_BOOT_IMAGE_VERSION_LEN]; /**< Device boot image version string */
	char                   boardName[NNPI_BOARD_NAME_LEN]; /**< Device board name */
	char                   partNumber[NNPI_PART_NUM_LEN]; /**< Device part number string */
	char                   serialNumber[NNPI_SERIAL_NUM_LEN]; /**< Device serial number string */
	uint32_t               numActiveContexts; /**< Num active inference contexts */
	uint16_t               fpga_rev; /**< fpga revision */
	uint32_t               stepping; /**< Stepping level */
} nnpimlDeviceStatus;

/**
 * @brief Different log categories that can be set to different log levels
 */
typedef enum {
	NNPIML_LOG_CATEGORY_ALL			= 0,
	NNPIML_LOG_CATEGORY_START_UP		= 1,
	NNPIML_LOG_CATEGORY_GO_DOWN		= 2,
	NNPIML_LOG_CATEGORY_DMA			= 3,
	NNPIML_LOG_CATEGORY_CONTEXT_STATE	= 4,
	NNPIML_LOG_CATEGORY_IPC			= 5,
	NNPIML_LOG_CATEGORY_CREATE_COMMAND	= 6,
	NNPIML_LOG_CATEGORY_SCHEDULE_COMMAND	= 7,
	NNPIML_LOG_CATEGORY_EXECUTE_COMMAND	= 8,
	NNPIML_LOG_CATEGORY_SERVICE		= 9,
	NNPIML_LOG_CATEGORY_ETH			= 10,
	NNPIML_LOG_CATEGORY_INFERENCE		= 11,
	NNPIML_LOG_CATEGORY_ICE			= 12,
	NNPIML_LOG_CATEGORY_GENERAL		= 13,
	NNPIML_LOG_CATEGORY_MAINTENANCE         = 14,
	NNPIML_LOG_CATEGORY_HWTRACE		= 15,
	NNPIML_LOG_CATEGORY_RUNTIME		= 16,
	NNPIML_LOG_CATEGORY_LAST
} nnpimlLogCategory;

/**
 * @brief Log message level
 */
typedef enum {
	NNPIML_LOG_LEVEL_NONE  = 0,    /**< Suppress all messages */
	NNPIML_LOG_LEVEL_ERR   = 1,    /**< Error log messages */
	NNPIML_LOG_LEVEL_WARN  = 2,    /**< Error and Warning log messages */
	NNPIML_LOG_LEVEL_INFO  = 3,    /**< Error, Warning and Information log messages */
	NNPIML_LOG_LEVEL_DEBUG = 4,    /**< Error, Warning, Info and Debug log messages */
	NNPIML_LOG_LEVEL_LAST
} nnpimlLogLevel;

/**
 * @brief Device error or warning event types
 */
typedef enum {
	NNPIML_DEVICE_STATE_CHANGED  = 1, /**< Device state has changed */
	NNPIML_DEVICE_EVENT_CRASHED  = 2, /**< The device has crashed and need to be reset */
	NNPIML_DEVICE_EVENT_THERMAL  = 3, /**< A thermal trip has been crossed */
	NNPIML_DEVICE_EVENT_PCIE_ERR = 4, /**< Uncorrected PCIE error threshold was reached */
	NNPIML_DEVICE_EVENT_ECC_ERR  = 5, /**< ECC corrected or uncorrected threshold was reached */
	NNPIML_DEVICE_EVENT_MCE_ERR  = 6,  /**< Generic MCE error (not ECC related) */
	NNPIML_DEVICE_EVENT_DRAM_ECC_ERR  = 7, /**< DRAM ECC corrected or uncorrected threshold was reached */
	NNPIML_DEVICE_EVENT_DMA_HANG_ERR = 8 /**< Device DMA hang has detected */

} nnpimlDeviceEventType;

/**
 * @enum nnpimlDeviceErrorClass
 *
 * Describes the class of the device error.
 */
typedef enum {
	NNPIML_DEVICE_ERROR_NO_ERROR = 0, /**< The event is not error related */
	NNPIML_DEVICE_ERROR_CORRECTABLE = 1, /**< A correctable error */
	NNPIML_DEVICE_ERROR_NON_FATAL   = 2, /**< A non-correctable non-fatal error */
	NNPIML_DEVICE_ERROR_FATAL       = 3, /**< A non-correctable fatal error */
} nnpimlDeviceErrorClass;

/**
 * @brief Error/Warning event threshold types
 */
typedef enum {
	NNPIML_EVENT_THRESHOLD_CORRECTED_ECC   = 1, /**< Corrected ECC errors threshold */
	NNPIML_EVENT_THRESHOLD_UNCORRECTED_ECC = 2  /**< Uncorrected ECC errors threshold */
} nnpimlEventThresholdType;

/**
 * @brief Describes a device error or warning event
 */
typedef struct {
	uint32_t              device_number;  /**< nnpi device number */
	nnpimlDeviceEventType type;           /**< nnpi device event type */
	nnpimlDeviceErrorClass errorClass;    /**< error class related to the event*/

	union {
		struct state_info {
			nnpimlDeviceState      state;      /**< Device operation state */
			nnpimlDeviceBootState  bootState;  /**< Device boot state */
			nnpimlDeviceFailReason failReason; /**< Device failure reason */
		} dev_state;

		struct thermal_info {
			uint8_t trip;         /**< Thermal trip point, 1 or 2 */
			uint8_t up_direction; /**< true if trip point crossed in the up direction */
		} thermal;                    /**< Thermal event info */

		struct power_info {
			uint8_t throttle;     /**< one if power throttled, zero otherwise */
		} power;                      /**< Power event info */
	} data;                               /**< Per-type event data */

} nnpimlDeviceEvent;

/**
 * @brief Ice dump levels
 */
typedef enum {
	NNPIML_ICE_DUMP_LEVEL_NONE   = 0, /**< ICE dump level none */
	NNPIML_ICE_DUMP_LEVEL_ERROR  = 1, /**< ICE dump level error */
	NNPIML_ICE_DUMP_LEVEL_DEBUG  = 2, /**< ICE dump level debug */
	NNPIML_ICE_DUMP_LEVEL_MAX	  /**< ICE dump level max value*/
} nnpimlIceDumpLevel;

/**
 * @brief Ice dump info
 */
typedef struct {
	char folder_name[NNPI_NAME_MAX];	/**< Ice dump's folder name */
	int64_t tv_sec;				/**< Seconds from epoc to the last time folder was modified*/
} nnpimlIceDumpInfo;

#ifdef __cplusplus
} // of extern "C"
#endif
