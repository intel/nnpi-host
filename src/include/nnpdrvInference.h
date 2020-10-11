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
 * @brief Header file defining host umd->kmd inference interface.
 * @file nnpdrvInference.h
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "nnpdrvTypes.h"

/**
 * @brief enum NNPResourceUsageFlags
 *
 * Values for a host or device resource usage, the resource usage
 * is a bitmask, so more than one value of that enum may be specified for
 * a resource.
 */
typedef enum {
	NNP_RESOURCE_USAGE_UNKNOWN         = 0,  /**< usage is unknown */
	NNP_RESOURCE_USAGE_NN_INPUT        = (1 << 0),  /**< resource used in network input */
	NNP_RESOURCE_USAGE_NN_OUTPUT       = (1 << 1),  /**< resource used in network output */
	NNP_RESOURCE_USAGE_NETWORK         = (1 << 2),  /**< resource is part of a network blob */
	NNP_RESOURECE_USAGE_FORCE_4G_ALLOC = (1 << 3),  /**< TLC accessible resource */
	NNP_RESOURECE_USAGE_ECC            = (1 << 4), /**< alloc resource in ecc */
	NNP_RESOURECE_USAGE_P2P_DST        = (1 << 5), /**< alloc resource in p2p */
	NNP_RESOURECE_USAGE_P2P_SRC        = (1 << 6), /**< resource is p2p src */
	NNP_RESOURECE_USAGE_LOCKLESS       = (1 << 7),/**< host resource is synchronized by application */
} NNPResourceUsageFlags;

/*TODO: Define mutually exclusive Resource allocation flags (Protected, Unprotected, P2P) */

/**
 * @brief enum NNPResourceUsageFlags
 *
 * Values for schedule flags.
 * It is a bitmask, so more than one value of that enum may be specified for
 * a command.
 */
typedef enum {
	NNP_SCHEDULE_SKIP_EXECUTION = (1 << 0), /**< the command should not be executed */
} NNPScheduleFlags;

/**
 * @brief component that caused a context critical error
 */
typedef enum {
	NNP_FAIL_OBJ_TYPE_NONE,    /**< No critical error condition */
	NNP_FAIL_OBJ_TYPE_CARD,    /**< Card s/w stack crashed or h/w hanged, reset required */
	NNP_FAIL_OBJ_TYPE_CONTEXT, /**< Card s/w stack specific for the context
				    *   has hanged. Might not be a card global
				    *   issue
				    */
	NNP_FAIL_OBJ_TYPE_COPY,    /**< Scheduled copy operation has failed */
	NNP_FAIL_OBJ_TYPE_INFREQ,  /**< Scheduled infer request has failed */
} NNPFailedObjType;

/**
 * @brief Network properties
 */
typedef enum {
	NNP_SERIAL_INF_EXEC,    /**< Serial inference execution */
	NNP_NETWORK_RESERVATION /**< Network resources reservation */
} NNPNetPropertiesType;

typedef uint64_t NNPInferContext;    /**< handle to an inference context */
typedef uint64_t NNPDeviceResource;  /**< handle to device resource      */
typedef uint64_t NNPCommandList;     /**< handle to command list         */
typedef uint64_t NNPCopyHandle;      /**< handle to a copy operation definition */
typedef uint64_t NNPDeviceNetwork;   /**< handle to a network resource   */
typedef uint64_t NNPInferRequest;    /**< handle to an infer request     */
typedef uint32_t NNPMarker;          /**< handle to host-to-card command stream marker */

/**
 * bit values for flags in nnpdrvCreateInferContextWithFlags
 */
#define NNP_ULT_CONTEXT      (1 << 0)
#define NNP_ULT_CONTEXT_LAST (1 << 1)

/**
 * fix size struct for inference request config data
 * sizeof(nnpdrvinfSchedParams) = sizeof(uint32_t),
 * equivalent to inferRequestSchedParams
 */
typedef struct {
	uint16_t   batchSize;
	uint8_t    priority; /* 0 == normal, 1 == high */
	uint8_t    debugOn : 1;
	uint8_t    collectInfo : 1;
	uint8_t    reserved : 6;
} nnpdrvinfSchedParams;


/**
 * @brief describes the reason for context critical error
 */
typedef struct {
	NNPCriticalError nnpCriticalError;      /**< Error code that caused the failure */
	NNPFailedObjType objType;               /**< Failed component: card,
						 *   context copy or infer request
						 */
	uint32_t errorMessageSize;              /**< error description buffer size */

	union {
		struct {
			NNPCopyHandle copyHandle;        /**< Failed copy handle */
		} copy;
		struct {
			NNPDeviceNetwork devnetHandle;   /**< Failed device network handle */
			NNPInferRequest  infreqHandle;   /**< Failed infer request handle */
		} infreq;
	} obj;

} NNPCriticalErrorInfo;


/**
 * @brief query number of NNP-I devices
 *
 * Queries the number of enumerated NNP-I devices in the system.
 * Note that it may include devices which did not yet booted or failed to boot.
 * It can return zero, if no devices were found.
 *
 * @param[out]  deviceNum  Number of NNP-I devices

 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT The provided deviceNum parameter is NULL
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 */
NNPError nnpdrvGetDeviceCount(uint32_t *deviceNum);

/**
 * @brief Query static NNP-I device information
 *
 * This function returns information about a specific NNP-I device.
 *
 * @param[in]  deviceNum  NNP-I device number
 * @param[out] outDevInfo Pointer to device info to be filled
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT The outDevInfo parameter is NULL
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_DEVICE   The boot state of the device is not ready to use
 *                              or the device number does not exist
 */
NNPError nnpdrvQueryDeviceInfo(uint32_t       deviceNum,
			       NNPDeviceInfo *outDevInfo);

/**
 * @brief Query dynamic NNP-I device state information
 *
 * This function returns state information about a specific NNP-I device.
 *
 * @param[in]  deviceNum     NNP-I device number
 * @param[out] outDevStatus  Pointer to device status to be filled
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT The outDevStatus parameter is NULL
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_DEVICE   The boot state of the device is not ready to use
 *                              or the device number does not exist
 */
NNPError nnpdrvQueryDeviceStatus(uint32_t         deviceNum,
				 NNPDeviceStatus *outDevStatus);


/**
 * @brief Creates an inference context
 *
 * The following creates an inference context on a NNP-I device.
 * The context is used as a connection to allocate device resources,
 * device networks as well as command work queue for sending infer requests
 * to the card for execution.
 * If the function succeeds, the return value is NNP_NO_ERROR and the outContext
 * contains the created context handle.
 *
 * @param[in]  deviceNum     NNP-I device number
 * @param[in]  flags         Context flags
 * @param[out] outContext    Created context handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT The outContext parameter is NULL
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_DEVICE   The device number does not exist
 * @retval NNP_DEVICE_NOT_READY The device state is not yet ready to accept
 *                              inference requsts
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_TOO_MANY_CONTEXTS The limit of number of contexts for a device
 *                               has reached.
 */
NNPError nnpdrvCreateInferContextWithFlags(uint32_t         deviceNum,
					   uint8_t          flags,
					   NNPInferContext *outContext);

/**
 * @brief Creates an inference context
 *
 * The following creates an inference context on a NNP-I device.
 * This function is the same as calling nnpdrvCreateInferContextWithFlags with
 * flags argument equal to zero.
 */
NNPError nnpdrvCreateInferContext(uint32_t         deviceNum,
				  NNPInferContext *outContext);

/**
 * @brief Destroys an inference context
 *
 * Destroys previously created inference context.
 * After that function returns the context handle as well as any
 * device resource handle created through that context cannot be used
 * in later function calls.
 *
 * @param[in]  ctx     Infer context handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 */
NNPError nnpdrvDestroyInferContext(NNPInferContext ctx);

/**
 * @brief Attempts to recovers a broken inference context
 *
 * If the context is non recoverable, the function will return an error.
 * In such case, nnpdrvDestroyInferContext() should be called.
 * Otherwise, recovery will succeed (all pending operations should be re-triggered)
 *
 * @param[in]  ctx     Infer context handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_DEVICE_ERROR     The device is in unrecovered state and needs a
 *                              h/w reset.
 * @retval NNP_BROKEN_MARKER    A synchronization with the device has failed
 *                              either due to out-of-memory condition or internal driver error.
 */
NNPError nnpdrvRecoverInferContext(NNPInferContext ctx);

/**
 * @brief Query NNP-I infer context information
 *
 * This function returns information about a specific infer context.
 *
 * @param[in]  ctx        Infer context handle
 * @param[out] outDevInfo Pointer to device info to be filled
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT The outInferContextInfo parameter is NULL
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CONTEXT  The infer context handle does not exist
 */
NNPError nnpdrvQueryInferContextInfo(NNPInferContext 	  ctx,
                                     NNPInferContextInfo *outInferContextInfo);

/**
 * @brief Write 64 bit value to device's SW trace.
 *
 * This function will trigger write to SW trace at the device of which 'ctx'
 * belongs to in the following formar:
 * user_data: key=<key> ctxID=<ctxID> user_data=<user_data>
 * Precondition: user_data event is enabled on the device.
 *
 * @param[in] ctx        Infer context handle
 * @param[in] key        Desctiptive string which will also be written to the trace
 *                       as way to identify the event in the output trace.
 *                       The key is formed by the first 6 characters only, or up to
 *                       a null character, if exists
 * @param[in] user_data  The value which will be written to the trace
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_INVALID_ARGUMENT If an empty string was given as key
 * @retval NNP_NO_SUCH_CONTEXT  The infer context handle does not exist
 */
NNPError nnpdrvInferContextTraceUserData(NNPInferContext ctx,
					 const char     *key,
					 uint64_t        user_data);

/**
 * @brief Creates a host resource
 *
 * Creates a resource object on the host which can be shared by all
 * contexts of the same process. The created resource is implicitly mapped
 * for user access.
 *
 * @param[in]  byteSize    The size of the resource in bytes.
 * @param[in]  usageFlags  Bitmask of values from NNPResourceUsageFlags enum
 * @param[out] outHostRes  Created handle of the host resource
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT byteSize is zero or outHostRes is NULL
 * @retval NNP_NOT_SUPPORTED    NNP_RESOURCE_USAGE_NETWORK is not supported
 *                              for host resource usage
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 */
NNPError nnpdrvCreateHostResource(uint64_t         byteSize,
				  uint32_t         usageFlags,
				  NNPHostResource *outHostRes);

/**
 * @brief Creates a host resource using file descriptor of dma_buf
 *
 * Creates a resource object on the host which can be shared by all
 * contexts of the same process.
 *
 * @param[in]  dmaBuf      The DMA buffer file descriptor to use as host resource.
 * @param[in]  usageFlags  Bitmask of values from NNPResourceUsageFlags enum
 * @param[out] outHostRes  Created handle to a host resource
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT dmaBuf is not dma-buf file descriptor or outHostRes is NULL
 * @retval NNP_NOT_SUPPORTED    NNP_RESOURCE_USAGE_NETWORK is not supported
 *                              for host resource usage
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 */
NNPError nnpdrvCreateDmaBufHostResource(int              dmaBuf,
					uint32_t         usageFlags,
					NNPHostResource *outHostRes);

/**
 * @brief Creates a host resource from user allocated buffer
 *
 * Creates a resource object on the host which can be shared by all
 * contexts of the same process.
 * The memory for the host resource is not being allocated, the function will
 * pin the memory pages of the user supplied buffer and this memory will be used
 * directly by the nnpi device. The function will fail if pinning the buffer to
 * memory fails.
 *
 * @param[in]  buf         Pointer to the user buffer to pin
 * @param[in]  byteSize    The size of the resource in bytes.
 * @param[in]  usageFlags  Bitmask of values from NNPResourceUsageFlags enum
 * @param[out] outHostRes  Created handle of the host resource
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT byteSize is zero, buf or outHostRes are NULL
 *                              or buf does not meet alignment restrictions.
 * @retval NNP_NOT_SUPPORTED    NNP_RESOURCE_USAGE_NETWORK is not supported
 *                              for host resource usage
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory or pinning fails
 */
NNPError nnpdrvCreateHostResourceFromBuf(const void      *buf,
					 uint64_t         byteSize,
					 uint32_t         usageFlags,
					 NNPHostResource *outHostRes);

/**
 * @brief Destroys a host resource
 *
 * Destroys a previously created host resource.
 * If the resource is mapped for user access, it will be implicitly unmapped by
 * that function.
 *
 * @param[in] hostRes Host resource handle to destroy
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_RESOURCE hostRes is not a host resource handle
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 */
NNPError nnpdrvDestroyHostResource(NNPHostResource hostRes);

/**
 * @brief Gets a host resource pointer for CPU access
 *
 * Returns a host resource CPU address.
 * On success the function returns NNP_NO_ERROR and a pointer to the resource
 * memory in outPtr parameter.
 *
 * @param[in]  hostRes  Host resource handle
 * @param[out] outPtr   Process virtual address mapped to the resource memory
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_RESOURCE hostRes is not a host resource handle
 */
NNPError nnpdrvGetHostResourceCPUAddress(NNPHostResource hostRes, void **outPtr);

/**
 * @brief Gets a host resource dma_buf file descriptor
 *
 * Returns a host resource dma_buf file descriptor if hostres based
 * on pre-allocated dma_buf.
 * On success the function returns NNP_NO_ERROR and a file descriptor
 * in outFD parameter.
 *
 * @param[in]  hostRes  Host resource handle
 * @param[out] outFD    File descriptor of pre-alloced dma_buf
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_RESOURCE hostRes is not a host resource handle
 */
NNPError nnpdrvGetHostResourceDmaBufFD(NNPHostResource hostRes, int *outFD);

/**
 * @brief Locks a host resource for CPU access
 *
 * Locks a host resource for CPU access.
 * This function may block if the resource is in use by any NNP-I device,
 * that is, there is pending device copy operation which reference the resource
 * and did not yet completed.
 * The timeoutUs parameter specify a timeout for blocking in microseconds
 * a value of 0 prevents any waiting, a value of UINT32_MAX will wait until the
 * resource is not busy with no timeout.
 *
 * This function may fail if the resource was used for a copy operation which failed.
 *
 * @param[in] hostRes   Host resource handle
 * @param[in] timeoutUs Wait timeout in microseconds
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_RESOURCE hostRes is not a host resource handle
 * @retval NNP_TIMED_OUT        The host resource is being referenced in
 *                              previously queued device copy operation that did
 *                              not yet complete.
 * @retval NNP_CONTEXT_BROKEN   Copy from/to this resource has failed.
 * @retval NNP_NOT_SUPPORTED    hostRes has attribute "lockless".
 */
NNPError nnpdrvLockHostResource(NNPHostResource hostRes,
				uint32_t        timeoutUs);

/**
 * @brief Unlocks previously locked host resource.
 *
 * This function unlocks a host resource for CPU access, the virtual address
 * pointer returned in previous call to lock the resource cannot be used after that
 * function returns.
 *
 * @param[in] hostRes Host resource handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_RESOURCE hostRes is not a host resource handle
 * @retval NNP_NOT_SUPPORTED    hostRes has attribute "lockless".
 */
NNPError nnpdrvUnlockHostResource(NNPHostResource hostRes);

NNPError nnpdrvGetCopyContext(NNPCopyHandle    copyHandle,
							  NNPInferContext *outCtx);
NNPError nnpdrvGetInferReqContext(NNPInferRequest  infReq,
								  NNPInferContext *outCtx);
/**
 * @brief Begins creation of a command list
 *
 * That function creates a command list on the host
 * to the given inference context.
 *
 * @param[in]  ctx              Inference context handle
 * @param[out] outCommandList   Created handle to a command list
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT outCommandList is NULL
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateCommandListBegin(NNPInferContext  ctx,
				      NNPCommandList  *outCommandList);

/**
 * @brief Ends creation of a command list
 *
 * That function finally creates command list on the NNP-I device connected
 * to the given inference context.
 *
 * @param[in] commandList       Command list handle to end creation
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateCommandListEnd(NNPCommandList commandList);

/**
 * @brief Appends an infer command handle into a command list object.
 *
 * @param[in] commandList       Command list handle
 * @param[in] copyHandle        Copy handle
 * @param[in] byteSize          bytes to copy, if zero, all resource is copied.
 * @param[in] priority          set priority for copy op: 1 for hight , 0 for normal
 * @param[in] flags             Bitmask from NNPScheduleFlags
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 * @retval NNP_NO_SUCH_COPY_HANDLE  The copyHandle does not exist
 */
NNPError nnpdrvCommandListAppendCopy(NNPCommandList        commandList,
				     NNPCopyHandle         copyHandle,
				     uint64_t              byteSize,
				     uint8_t               priority,
				     uint32_t              flags);

/**
 * @brief Appends an infer command handle into a command list object.
 *
 * @param[in] commandList       Command list handle
 * @param[in] infReq            Device infer request handle
 * @param[in] schedParams       Inference request schedule specific configuration (may be NULL)
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 * @retval NNP_NO_SUCH_INFREQ_HANDLE  The inferReq handle does not exist
 */
NNPError nnpdrvCommandListAppendInferRequest(NNPCommandList        commandList,
					     NNPInferRequest       infReq,
					     nnpdrvinfSchedParams *schedParams);

/**
 * @brief Destroys previously created command list
 *
 * @param[in] commandList       Command list handle to destroy
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 */
NNPError nnpdrvDestroyCommandList(NNPCommandList commandList);

/**
 * @brief Overwrites copy command parameters for next schedule.
 *
 * @param[in] commandList       Command list handle
 * @param[in] copy_idx          Index of copy command in Command list
 * @param[in] byteSize          bytes to copy, if zero, all resource is copied
 * @param[in] priority          set priority for copy op:
 *                              1 for hight, 0 for normal
 * @param[in] flags             Bitmask from NNPScheduleFlags
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 * @retval NNP_NO_SUCH_COPY_HANDLE  The command at given index is not copy
 * @retval NNP_INVALID_ARGUMENT copy_idx is out of range
 */
NNPError nnpdrvCommandListOverwriteCopy(NNPCommandList        commandList,
					uint16_t              copy_idx,
					uint64_t              byteSize,
					uint8_t               priority,
					uint32_t              flags);

/**
 * @brief Overwrites inference request command parameters for next schedule.
 *
 * @param[in] commandList       Command list handle
 * @param[in] infreq_idx        Index of infer request command in Command list
 * @param[in] schedParams       Inference request schedule specific
 *                              configuration (may be NULL)
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 * @retval NNP_NO_SUCH_INFREQ_HANDLE  The command at given index is not infreq
 * @retval NNP_INVALID_ARGUMENT infreq_idx is out of range
 */
NNPError nnpdrvCommandListOverwriteInferRequest(NNPCommandList        commandList,
						uint16_t              infreq_idx,
						nnpdrvinfSchedParams *schedParams);

/**
 * @brief Schedule of a command list
 *
 * This function schedules command list on the NNP-I device connected
 * to the given inference context.
 *
 * @param[in] commandList       Command list handle to schedule
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvScheduleCommandList(NNPCommandList commandList);

/**
 * @brief Wait for command list to complete
 *
 * This function waits until command list finish to run.
 *
 * @param[in] commandList       Command list handle
 * @param[in] timeoutUs         Timeout in micro seconds
 * The timeoutUs parameter specify a timeout for blocking in microseconds
 * a value of UINT32_MAX will make the function block without any timeout
 * and will be returned only on completion or if a critical error occurred
 * on the context.
 * @param[out] errors           Array of critical errors information, should be
 *                              pre-allocated
 * @param[in/out] numErrors     input size of provided "errors" array
 *                              output actual number of elements filled
 *                              or number of elements needed if the array
 *                              is too small
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvWaitCommandList(NNPCommandList commandList,
			       uint32_t timeoutUs,
			       NNPCriticalErrorInfo *errors,
			       uint32_t *numErrors);

/**
 * @brief Retrives error message buffer of critical error state in command list
 *
 * @param[in] commandList       Command list handle
 * @param[in] index             Index of error
 * @param[in] buf               Pointer of buffer to be filled with error message
 * @param[in] buf_size          Size of buffer pointed by buf
 * @param[out] out_buf_size     Returns the actual size of the error message
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 */
NNPError nnpdrvCommandListGetErrorMessage(NNPCommandList commandList,
					  uint32_t       index,
					  void          *buf,
					  uint32_t       buf_size,
					  uint32_t      *out_buf_size);

/**
 * @brief Clear command list error state
 *
 * This function clears command list error state to be able to schedule this
 * command list again.
 *
 * @param[in] commandList       Command list handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_CMDLIST  The commandList handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCommandListClearErrorState(NNPCommandList commandList);

/**
 * @brief Create a device resource
 *
 * That function creates a device resource on the NNP-I device connected
 * to the given inference context.
 *
 * @param[in]  ctx        Inference context handle
 * @param[in]  byteSize   Resource size in bytes
 * @param[in]  align      Resource size alignment in bytes(PAGE_SIZE units)
 * @param[in]  usageFlags Bitmask of values from NNPResourceUsageFlags enum
 * @param[out] outDevRes  Created handle to a device resource
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT byteSize is zero, outDevRes is NULL or
 *                              usageFlags is not supported
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateDeviceResource(NNPInferContext    ctx,
				    uint64_t           byteSize,
				    uint64_t           align,
				    uint32_t           usageFlags,
				    NNPDeviceResource *outDevRes);

/**
 * @brief Create device resource FIFO
 *
 * That function creates a device resource FIFO on the NNP-I device connected
 * to the given inference context.
 *
 * @param[in]  ctx        Inference context handle
 * @param[in]  elemByteSize   Element size in bytes
 * @param[in]  depth          Number of elements
 * @param[in]  align          resource size alignment (PAGE_SIZE_ units)
 * @param[in]  usageFlags Bitmask of values from NNPResourceUsageFlags enum
 * @param[out] outDevRes  Created handle to a device resource
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT byteSize is zero, outDevRes is NULL or
 *                              usageFlags is not supported
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateDeviceResourceFIFO(NNPInferContext    ctx,
					uint64_t           elemByteSize,
					uint32_t           depth,
					uint64_t           align,
					uint32_t           usageFlags,
					NNPDeviceResource *outDevRes);
/**
 * @brief Create a device resource and load it with the content of a file
 *
 * That function creates a device resource on the NNP-I device connected
 * to the given inference context.
 * The size of the resource is the size of the file specified by fileName,
 * the content of the file is read and loaded into the device resource memory.
 *
 * @param[in]  ctx        Inference context handle
 * @param[in]  fileName   File name holding the desired resource data
 * @param[in]  align      resource size alignment (PAGE_SIZE_ units)
 * @param[in]  usageFlags Bitmask of values from NNPResourceUsageFlags enum
 * @param[out] outDevRes  Created handle to a device resource
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT fileName is NULL or usageFlags not supported
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateDeviceResourceFromFile(NNPInferContext    ctx,
					    const char        *fileName,
					    uint64_t           align,
					    uint32_t           usageFlags,
					    NNPDeviceResource *outDevRes);

/**
 * @brief Loads data into a device resource
 *
 * That function copies the content of the given buffer, pointed by the
 * data argument into the device resource starting at the given offset.
 * This function must not be called with device resource, that already is used
 * by some device network. The behavior is undefined.
 *
 * @param[in]  devRes     Handle to the target device resource
 * @param[in]  offset     starting offset in the target device resource
 * @param[in]  data       Buffer that should loaded into the device resource
 * @param[in]  dataSize   Buffer size to load
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT data is NULL
 * @retval NNP_NO_SUCH_RESOURCE devRes does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvDeviceResourceSubLoad(NNPDeviceResource devRes,
				     uint64_t          offset,
				     const void       *data,
				     uint64_t          dataSize);

typedef ssize_t (*NNPStreamReadCb)(void  *stream_ctx,
				   void  *dst,
				   size_t size);

/**
 * @brief Loads data into a device resource from a "stream"
 *
 * That function reads data from a stream using the supplied read_cb
 * callback and copy it into the device resource starting at the given offset.
 * The stream ends when the callback function returns zero or negative value,
 * zero means the end of the stream and negative value means an error, in which
 * case the function will return NNP_IO_ERROR.
 * This function must not be called with device resource, that already is used
 * by some device network. The behavior is undefined.
 *
 * @param[in]  devRes     Handle to the target device resource
 * @param[in]  offset     starting offset in the target device resource
 * @param[in]  read_cb    Callback which read from the "stream"
 * @param[in]  stream_ctx Stream context, passed to callback as is.
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT data is NULL
 * @retval NNP_NO_SUCH_RESOURCE devRes does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvDeviceResourceSubLoadFromStream(NNPDeviceResource devRes,
					       uint64_t          offset,
					       NNPStreamReadCb   read_cb,
					       void             *stream_ctx);

/**
 * @brief Destroys previously created device resource
 *
 * @param[in] devRes Device resource handle to destroy
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_RESOURCE The devRes handle does not exist
 */
NNPError nnpdrvDestroyDeviceResource(NNPDeviceResource devRes);


/**
 * @brief Mark destination peer to peer device resource dirty
 *
 * @param[in] devRes Device resource handle to mark
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_RESOURCE The devRes handle does not exist
 */
NNPError nnpdrvMarkDeviceResourceDirty(NNPDeviceResource devRes);

/**
 * @brief Creates a host-to-device copy operation handle.
 *
 * Creates a copy operation handle which defines a DMA copy operation
 * to copy the given host resource to the given device resource.
 *
 * @param[in]  ctx        Inference context handle
 * @param[in]  hostRes    Host resource handle
 * @param[in]  devRes     Device resource handle
 * @param[out] outHandle  Output copy handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT outHandle is NULL or
 *                              devRes belongs to another context
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_NO_SUCH_RESOURCE devRes or hostRes does not exist
 * @retval NNP_INCOMPATIBLE_RESOURCES resources are not compatible for a copy
 *                                    operation (i.e. different size,
 *                                    incompatible copy direction)
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateHostToDeviceCopyHandle(NNPInferContext   ctx,
					    NNPHostResource   hostRes,
					    NNPDeviceResource devRes,
					    NNPCopyHandle    *outHandle);

/**
 * @brief Creates a device-to-host copy operation handle.
 *
 * Creates a copy operation handle which defines a DMA copy operation
 * to copy the given device resource to the given host resource.
 *
 * @param[in] ctx        Inference context handle
 * @param[in] devRes     Device resource handle
 * @param[in] hostRes    Host resource handle
 * @param[out] outHandle Output copy handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT outHandle is NULL or
 *                              devRes belongs to another context
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_NO_SUCH_RESOURCE devRes or hostRes does not exist
 * @retval NNP_INCOMPATIBLE_RESOURCES resources are not compatible for a copy
 *                                    operation (i.e. different size,
 *                                    incompatible copy direction)
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateDeviceToHostCopyHandle(NNPInferContext   ctx,
					    NNPDeviceResource devRes,
					    NNPHostResource   hostRes,
					    NNPCopyHandle    *outHandle);

/**
 * @brief Creates a device-to-device copy operation handle.
 *
 * Creates a copy operation handle which defines a DMA copy operation
 * to copy the given device resource from to the given device resource to.
 *
 * @param[in]  ctx        Inference context handle on the source card
 * @param[in]  to         Device resource handle of the destination
 * @param[in]  from       Device resource handle of the source
 * @param[out] outHandle  Output copy handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT outHandle is NULL or
 *                              resource belongs to another context
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_NO_SUCH_RESOURCE to or from resources does not exist
 * @retval NNP_INCOMPATIBLE_RESOURCES resources are not compatible for a copy
 *                                    operation (i.e. different size,
 *                                    incompatible copy direction)
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateDeviceToDeviceCopyHandle(NNPInferContext     ctx,
					      NNPDeviceResource   to,
					      NNPDeviceResource   from,
					      NNPCopyHandle       *outHandle);

/**
 * @brief Destroys a previously created copy handle.
 *
 * Destroys a copy operation handle.
 *
 * @param[in] copyHandle Copy handle to destroy
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_COPY_HANDLE copyHandle does not exist
 */
NNPError nnpdrvDestroyCopyHandle(NNPCopyHandle   copyHandle);


/**
 * @brief Creates a device network
 *
 * Creates a device network on the NNP-I device connected to the given
 * Infer context.
 *
 * @param[in]  ctx               Inference context handle
 * @param[in]  netBlobFilename   Filename containing compiled network blob
 * @param[in]  netConfigData     Pointer to network configuration data
 * @param[in]  netConfigDataSize Size of network configuration data block
 * @param[out] outNetHandle      Created device network handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT outNetHandle or netBlobFilename are NULL,
 *                              or netConfigDataSize is greater than zero and
 *                              netConfigData is NULL.
 * @retval NNP_NOT_SUPPORTED    netConfigDataSize exceeds PAGE_SIZE
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateDeviceNetwork(NNPInferContext   ctx,
				   const char	    *netBlobFilename,
				   void	            *netConfigData,
				   uint32_t	     netConfigDataSize,
				   NNPDeviceNetwork *outNetHandle);

/**
 * @brief Creates a device network using populated device resources
 *
 * Creates a device network on the NNP-I device connected to the given
 * Infer context. Network data is given by the array of the already populated
 * device resources.
 *
 * @param[in]  ctx               Inference context handle
 * @param[in]  devResArray       Array of device resources
 * @param[in]  devResArrayLen    Number of devices resources
 * @param[in]  netConfigData     Pointer to configuration data block
 * @param[in]  netConfigDataSize Size of network configuration data block
 * @param[out] outNetHandle      Created device network handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT outNetHandle are NULL,
 *                              or netConfigDataSize is greater than zero and
 *                              netConfigData is NULL.
 * @retval NNP_NOT_SUPPORTED    netConfigDataSize exceeds maximum limit
 * @retval NNP_INCOMPATIBLE_RESOURCES One of the device resources has no
 *                              NNP_RESOURCE_USAGE_NETWORK in its usage flags.
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateDeviceNetworkWithResources(NNPInferContext    ctx,
						NNPDeviceResource *devResArray,
						uint32_t           devResArrayLen,
						void              *netConfigData,
						uint32_t           netConfigDataSize,
						NNPDeviceNetwork  *outNetHandle);

/**
 * @brief Adds network device resources to exiting network
 *
 * Adds a list of additional device resources to a device network after it got
 * created. The function will fail if infer requests are already created in the
 * network.
 *
 * @param[in]  devNet            Device network handle
 * @param[in]  devResArray       Array of device resources
 * @param[in]  devResArrayLen    Number of devices resources
 * @param[in]  configData        Pointer to configuration data block
 * @param[in]  configDataSize    Size of network configuration data block
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT configDataSize is greater than zero and
 *                              configData is NULL.
 * @retval NNP_NOT_SUPPORTED    configDataSize exceeds maximum limit
 * @retval NNP_INCOMPATIBLE_RESOURCES One of the device resources has no
 *                              NNP_RESOURCE_USAGE_NETWORK in its usage flags.
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_NO_SUCH_NETWORK  The network handle does not exist
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvDeviceNetworkAddResources(NNPDeviceNetwork   devNet,
					 NNPDeviceResource *devResArray,
					 uint32_t           devResArrayLen,
					 void              *configData,
					 uint32_t           configDataSize);

NNPError nnpdrvRuntimeControl(NNPDeviceNetwork   devNet,
			      void              *ioBuffer,
			      uint32_t          *ioBufferSize,
			      NNPDeviceResource *ioDevResArray,
			      uint32_t          *ioDevResArrayLen);

/**
 * @brief Update a device network
 *
 * Updates an existing device network on the NNP-I device connected to the given
 * Infer context.
 *
 * @param[in]  devNet            Device network handle
 * @param[in]  netBlobFilename   Filename containing compiled network blob
 * @param[in]  netConfigData     Pointer to network configuration data
 * @param[in]  netConfigDataSize Size of network configuration data block
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_INVALID_ARGUMENT netBlobFilename is NULL,
 *                              or netConfigDataSize is greater than zero and
 *                              netConfigData is NULL.
 * @retval NNP_INVALID_EXECUTABLE_NETWORK_BINARY Network blob file is
 *                              corrupted/invalid.
 * @retval NNP_NOT_SUPPORTED    netConfigDataSize exceeds PAGE_SIZE
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvUpdateDeviceNetwork(NNPDeviceNetwork devNet,
				   const char	   *netBlobFilename,
				   void		   *netConfigData,
				   uint32_t	    netConfigDataSize);

/**
 * @brief Destroys a previously created device network
 *
 * Destroys a device network object.
 *
 * @param[in]  devNet            Device network handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_NETWORK  devNet does not exist
 */
NNPError nnpdrvDestroyDeviceNetwork(NNPDeviceNetwork devNet);

/**
 * @brief reserve ICE resources for the specified Network
 *
 * @param[in]  devNet              Device network handle
 * @param[in]  timeoutUs           timeout in micro seconds
 * The timeoutUs parameter specify a timeout for blocking in microseconds
 * a value of UINT32_MAX will make the function block without any timeout
 * and will be returned only on completion or if a critical error occurred
 * on the context.
 *
 * @retval NNP_NO_ERROR               Success
 * @retval NNP_INSUFFICIENT_RESOURCES The device doesn't have enough available resources.
 * @retval NNP_TIMED_OUT              The timeout expired before the operation completed.
 */
NNPError nnpdrvDeviceNetworkReserveExecResources(NNPDeviceNetwork devNet, uint32_t timeoutUs);

/**
 * @brief release ICE resources for the specified Network
 *
 * @param[in]  devNet       Device network handle
 *
 * @retval NNP_NO_ERROR     Success
 */
NNPError nnpdrvDeviceNetworkReleaseExecResources(NNPDeviceNetwork devNet);

/**
 * @brief set network property
 *
 * @param[in]  devNet              Device network handle
 * @param[in]  property            Network property to be set
 * @param[in]  property_val        Network property value
 * @param[in]  timeoutUs           timeout in micro seconds
 * The timeoutUs parameter specify a timeout for blocking in microseconds
 * a value of UINT32_MAX will make the function block without any timeout
 * and will be returned only on completion or if a critical error occurred
 * on the context.
 *
 * @retval NNP_NO_ERROR               Success
 * @retval NNP_INSUFFICIENT_RESOURCES The device doesn't have enough available resources.
 * @retval NNP_TIMED_OUT              The timeout expired before the operation completed.
 */
NNPError nnpdrvDeviceSetNetworkProperty(NNPDeviceNetwork devNet, NNPNetPropertiesType property, uint32_t property_val, uint32_t timeoutUs);

/**
 * @brief Creates an inference request
 *
 * Creates an inference request on the NNP-I device connected to the given
 * infer context.
 *
 * @param[in]  devNet             Device network handle
 * @param[in]  configData         Pointer to infer request config data block
 * @param[in]  configDataSize     Size of config data block
 * @param[in]  numInputs          Number of input device resources
 * @param[in]  maxExecConfigSize  Maximum possible infer execution config size
 * @param[in]  inputDevResources  Array of input device resource handles
 * @param[in]  numOutputs         Number of output device resources
 * @param[in]  outputDevResources Array of output device resource handles
 * @param[out] outHandle          Created infer request handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_NETWORK  devNet does not exist
 * @retval NNP_INCOMPLETE_NETWORK devNet object has no device resources
 * @retval NNP_NO_SUCH_RESOURCE Invalid device resource handle in one of the
 *                              inputs or outputs
 * @retval NNP_INFER_MISSING_RESOURCE The device network expect either more or
 *                              or less resources in the input or output
 *                              resource lists.
 * @retval NNP_NOT_SUPPORTED    numInputs and numOutputs and configDataSize
 *                              are too big or maxExecConfigSize is too big
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvCreateInferRequest(NNPDeviceNetwork   devNet,
				  void		    *configData,
				  uint32_t	     configDataSize,
				  uint32_t           maxExecConfigSize,
				  uint32_t	     numInputs,
				  NNPDeviceResource *inputDevResources,
				  uint32_t	     numOutputs,
				  NNPDeviceResource *outputDevResources,
				  NNPInferRequest   *outHandle);

/**
 * @brief Destroys an infer request
 *
 * Destroys a previously created infer request.
 *
 * @param[in]  inferReq          Device infer request handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_INFREQ_HANDLE  The inferReq handle does not exist
 */
NNPError nnpdrvDestroyInferRequest(NNPInferRequest inferReq);

/**
 * @brief Schedule an infer request execution
 *
 * Queue an infer request execution on the NNP-I device connected to
 * the given infer context.
 *
 * @param[in]  infReq              Device infer request handle
 * @param[in]  schedParams         Inference request schedule specific configuration (may be NULL)
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_INFREQ_HANDLE  The inferReq handle does not exist
 * @retval NNP_INVALID_ARGUMENT schedConfigDataSize is grater than zero but
 *                              schedConfigData is NULL or
 *                              schedConfigDataSize exceeds maximum
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvScheduleInferReq(NNPInferRequest infReq,
				nnpdrvinfSchedParams *schedParams);

/**
 * @brief Schedule a copy operation for execution
 *
 * Queue a host-to-device or device-to-host copy operation for execution on the
 * NNP-I device connected to the given infer context.
 *
 * @param[in]  copyHandle        Copy handle
 * @param[in]  byteSize          bytes to copy, if zero, all resource is copied.
 * @param[in]  priority          set priority for copy op: 1 for hight , 0 for normal
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_COPY_HANDLE  The copyHandle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 * @retval NNP_HOSTRES_BROKEN   The host resource used by the copy was used
 *                              by another copy on a different context and that
 *                              copy has failed.
 */
NNPError nnpdrvScheduleCopy(NNPCopyHandle copyHandle, uint64_t byteSize, uint8_t priority);

/**
 * @brief Gets a marker handle which marks the current command position in the context command queue.
 *
 * The function returns a marker handle which marks the current command position in the
 * context command queue. This marker handle can be used for polling or waiting until
 * all scheduled device commands up to this marker has been processed and completed by the device.
 *
 * @param[in]  ctx               Inference context handle
 * @param[out] outMarker         the marker handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_INVALID_ARGUMENT outMarker is NULL
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_OUT_OF_MEMORY    System ran out of memory
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvGetMarker(NNPInferContext  ctx,
			 NNPMarker	 *outMarker);

/**
 * @brief Waits until all previous operations are completed.
 *
 * Waits until all previous operations on the given context has been
 * processes and completed.
 * In case the card holding the context is in error state, the function
 * returns before the operations are completed and the error code is returned.
 *
 * @param[in]  ctx               Inference context handle
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvFinish(NNPInferContext ctx);

/**
 * @brief wait or poll for command stream marker
 *
 * Waits until all commands scheduled before the specified marker has
 * been processes and completed on the NNP-I device.
 *
 * The timeoutUs parameter specify a timeout for blocking in microseconds
 * a value of UINT32_MAX will make the function block without any timeout
 * and will be returned only on completion or if a critical error occurred
 * on the context.
 *
 * @param[in] ctx        Inference context handle
 * @param[in] marker     The command streamer marker to wait for
 * @param[in] timeoutUs  Wait timeout in microseconds
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 * @retval NNP_TIMED_OUT        The timeout expired before the marker has
 *                              completed.
 * @retval NNP_BROKEN_MARKER    The marker handle was failed to be created
 * @retval NNP_CONTEXT_BROKEN   Context is in broken state and must be either
 *                              recovered using nnpdrvRecoverInferContext or
 *                              destroyed.
 */
NNPError nnpdrvWaitForMarker(NNPInferContext ctx,
			     NNPMarker	     marker,
			     uint32_t	     timeoutUs);

/**
 * @brief returns the last critical error detected on the given context.
 *
 * @param[in]  ctx               Inference context handle
 * @param[out] outErrorInfo	 Critical error information output
 *
 * @retval NNP_NO_ERROR         Operation succeeded, check
 *                              outErrorInfo->nnpCriticalError for any error
 *                              condition.
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred and error
 *                              condition could not be retrieved.
 */
NNPError nnpdrvGetError(NNPInferContext ctx,
			NNPCriticalErrorInfo *outErrorInfo);

/**
 * @brief Wait until a fatal critical error exist on the context.
 *
 * This function will block the calling thread until a critical error has
 * occurred on the context. A Critical error is an error that cannot be recovered
 * and requires deletion of the context and all its underlying objects (device resources,
 * networks, infer requests, etc.)
 *
 * The timeoutUs parameter specify a timeout for blocking in microseconds
 * a value of UINT32_MAX will make the function block without any timeout and will be returned
 * only when a critical error occurred on the context.
 * A value of 0 to timeoutUs prevent any blocking and will make
 * the function return immediatley.
 *
 * @param[in] ctx		Inference context handle
 * @param[in] timeoutUs Wait	timeout in microseconds
 * @param[out] outErrorInfo	Critical error information output
 *
 * @retval NNP_NO_ERROR         Operation succeeded, check
 *                              outErrorInfo->nnpCriticalError for any error
 *                              condition.
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred and error
 *                              condition could not be retrieved.
 * @retval NNP_TIMED_OUT        The timeout expired before the critical error
 *                              has occurred in the context.
 */
NNPError nnpdrvWaitForCriticalError(NNPInferContext ctx,
				    uint32_t	    timeoutUs,
				    NNPCriticalErrorInfo *outErrorInfo);


/**
 * @brief Retrives error message buffer of the critical error message
 *
 * @param[in] ctx		Inference context handle
 * @param[in] buf               Pointer of buffer to be filled with error message
 * @param[in] buf_size          Size of buffer pointed by buf
 * @param[out] out_buf_size     Returns the actual size of the error message
 *
 * @retval NNP_NO_ERROR         Success
 * @retval NNP_NO_SUCH_CONTEXT  The context handle does not exist
 * @retval NNP_IO_ERROR         Internal driver error has occurred
 */
NNPError nnpdrvGetCriticalErrorMessage(NNPInferContext ctx,
				       void           *buf,
				       uint32_t        buf_size,
				       uint32_t       *out_buf_size);
/**
 * @brief close connecttion with KMD module.
 *
 * This function will close connection with KMD module and will destroy all the remaining inference driver objects.
 * It should be the last to call of this API.

 * @returns void
 */
void nnpdrvFin(void);

#ifdef __cplusplus
} // of extern "C"
#endif
