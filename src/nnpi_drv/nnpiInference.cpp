/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpdrvInference.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "nnpiDevice.h"
#include "nnpiUtils.h"
#include "nnpiInfContext.h"
#include "nnpiDevRes.h"
#include "nnpiCopyCommand.h"
#include "nnpiDevNet.h"
#include "nnpiInfReq.h"
#include "nnpiml_types.h"
#include "nnpiCommandList.h"
#include "nnp_log.h"
//#include "nnpi_umd_internal.h"
#include "safe_lib.h"
//#include "nnpdrvMaintenance.h"
//#include "nnpiMgmt.h"

static nnpiHandleMap<nnpiInfContext, uint64_t> s_contexts;
static nnpiHandleMap<nnpiDevRes, uint64_t> s_devres;
static nnpiHandleMap<nnpiCopyCommand, uint64_t> s_copy;
static nnpiHandleMap<nnpiDevNet, uint64_t> s_networks;
static nnpiHandleMap<nnpiInfReq, uint64_t> s_infreqs;
static nnpiHandleMap<nnpiCommandList, uint64_t> s_cmdlists;
static bool s_atexit_installed = false;

static void nnpdrvFin_no_wait(void)
{
	uint64_t hdl;

	while( s_cmdlists.get_first(hdl) )
		nnpdrvDestroyCommandList((NNPCommandList)hdl);

	while( s_infreqs.get_first(hdl) )
		nnpdrvDestroyInferRequest((NNPInferRequest)hdl);

	while( s_networks.get_first(hdl) )
		nnpdrvDestroyDeviceNetwork((NNPDeviceNetwork)hdl);

	while( s_copy.get_first(hdl) )
		nnpdrvDestroyCopyHandle((NNPCopyHandle)hdl);

	while( s_devres.get_first(hdl) )
		nnpdrvDestroyDeviceResource((NNPDeviceResource)hdl);

	nnpiHostRes::handle_map.clear();

	while( s_contexts.get_first(hdl) )
		nnpdrvDestroyInferContext((NNPInferContext)hdl);

	// Wait all contexts to be destroyed
	nnpiActiveContexts::wait_all();
}

void nnpdrvFin(void)
{
	nnpdrvFin_no_wait();
}

NNPError nnpdrvGetDeviceCount(uint32_t *deviceNum)
{
	int max_dev;

	if (!deviceNum)
		return NNP_INVALID_ARGUMENT;

	max_dev = nnpiDevice::findMaxDeviceNumber();
	if (max_dev < 0)
		return NNP_NO_SUCH_DEVICE;

	*deviceNum = max_dev + 1;

	return NNP_NO_ERROR;
}

#if 0 //GUY
NNPError nnpdrvQueryDeviceInfo(uint32_t deviceNum, NNPDeviceInfo *outDevInfo)
{
	char line[128];
	NNPError ret;

	if (!outDevInfo)
		return NNP_INVALID_ARGUMENT;

	ret = read_sysfs_val(deviceNum, "ice_units", 'u', &outDevInfo->numIceDevices);
	if (ret != NNP_NO_ERROR)
		return ret;

	ret = read_sysfs_val(deviceNum, "total_unprotected_mem", 'u',  &outDevInfo->totalUnprotectedMemory);
	if (ret != NNP_NO_ERROR)
		return ret;

	ret = read_sysfs_val(deviceNum, "total_protected_mem", 'u',  &outDevInfo->totalEccMemory);
	if (ret != NNP_NO_ERROR)
		return ret;

	ret = read_sysfs_string(deviceNum, "protocol_version", line, sizeof(line));
	if (ret != NNP_NO_ERROR)
		return ret;
	if (sscanf(line, "%hhu.%hhu.%hhu",
		   &outDevInfo->driverVersionMajor,
		   &outDevInfo->driverVersionMinor,
		   &outDevInfo->driverVersionDot) != 3)
		return NNP_IO_ERROR;

	outDevInfo->fwVersionMajor = 0;
	outDevInfo->fwVersionMinor = 0;
	outDevInfo->fwVersionDot = 0;

	uint32_t stepping;
	ret = read_sysfs_val(deviceNum, "card_stepping", 'u', &stepping);
	if (ret != NNP_NO_ERROR)
		return ret;
	outDevInfo->stepping = (uint8_t)stepping;

	return NNP_NO_ERROR;
}

NNPError nnpdrvQueryDeviceStatus(uint32_t deviceNum, NNPDeviceStatus *outDevStatus)
{
	nnpimlDeviceStatus status;
	NNPError ret;

	if (!outDevStatus)
		return NNP_INVALID_ARGUMENT;

	ret = nnpmntGetDeviceStatus(deviceNum, &status);
	if (ret != NNP_NO_ERROR)
		return ret;

	switch (status.bootState) {
	case NNPIML_BOOT_STATE_FAILED:
		outDevStatus->bootState = NNP_BOOT_FAILED;
		break;
	case NNPIML_BOOT_STATE_DEVICE_READY:
		outDevStatus->bootState = NNP_BOOT_DEVICE_READY;
		break;
	case NNPIML_BOOT_STATE_DRIVER_READY:
		outDevStatus->bootState = NNP_BOOT_DRIVER_READY;
		break;
	case NNPIML_BOOT_STATE_BOOT_STARTED:
		outDevStatus->bootState = NNP_BOOT_BOOT_STARTED;
		break;
	case NNPIML_BOOT_STATE_BIOS_UPDATE_STARTED:
		outDevStatus->bootState = NNP_BOOT_BIOS_UPDATE_STARTED;
		break;
	case NNPIML_BOOT_STATE_BIOS_READY:
		outDevStatus->bootState = NNP_BOOT_BIOS_READY;
		break;
	case NNPIML_BOOT_STATE_RECOVERY_BIOS_READY:
		outDevStatus->bootState = NNP_BOOT_RECOVERY_BIOS_READY;
		break;
	case NNPIML_BOOT_STATE_UNKNOWN:
	default:
		outDevStatus->bootState = NNP_BOOT_UNKNOWN;
		break;
	}

	switch (status.failReason) {
	case NNPIML_FAILURE_NOT_FAILED:
		outDevStatus->failReason = NNP_FAILURE_NOT_FAILED;
		break;
	case NNPIML_FAILURE_FAILED_VERSION:
		outDevStatus->failReason = NNP_FAILURE_FAILED_VERSION;
		break;
	case NNPIML_FAILURE_BOOT_FAILED:
		outDevStatus->failReason = NNP_FAILURE_BOOT_FAILED;
		break;
	case NNPIML_FAILURE_HOST_DRIVER_ERROR:
		outDevStatus->failReason = NNP_FAILURE_HOST_DRIVER_ERROR;
		break;
	case NNPIML_FAILURE_KERNEL_CRASH:
		outDevStatus->failReason = NNP_FAILURE_KERNEL_CRASH;
		break;
	case NNPIML_FAILURE_BIOS_UPDATE_REQUIRED:
		outDevStatus->failReason = NNP_FAILURE_BIOS_UPDATE_REQUIRED;
		break;
	case NNPIML_FAILURE_BIOS_UPDATE_FAILED:
		outDevStatus->failReason = NNP_FAILURE_BIOS_UPDATE_FAILED;
		break;
	case NNPIML_FAILURE_PCI_ERROR:
	case NNPIML_FAILURE_RESET_IN_PROGRESS:
	case NNPIML_FAILURE_FATAL_MCE_ERROR:
	default:
		outDevStatus->failReason = NNP_FAILURE_MAX;
		break;
	}

	outDevStatus->numActiveContexts = status.numActiveContexts;

	return NNP_NO_ERROR;
}
#endif

void nnpdrvAtExit()
{
	nnpdrvFin_no_wait();

	nnpiDevice::clear_devices(false);
}

NNPError nnpdrvCreateInferContextWithFlags(uint32_t         deviceNum,
					   uint8_t          flags,
					   NNPInferContext *outContext)
{
	nnpiInfContext::ptr ctx;
	NNPError ret;

	if (!outContext)
		return NNP_INVALID_ARGUMENT;

	ret = nnpiInfContext::create(deviceNum,
				     flags,
				     ctx);
	if (ret == NNP_NO_ERROR) {
		*outContext = s_contexts.makeHandle(ctx);
		ctx->set_user_hdl(*outContext);
		ctx->send_user_handle(INF_OBJ_TYPE_CONTEXT, 0, 0, *outContext);

		//
		// Install nnpdrvAtExit as atexit handler, if not yet installed
		//
		if (!s_atexit_installed) {
			std::lock_guard<std::mutex> lock(s_contexts.mutex());
			if (!s_atexit_installed) {
				atexit(nnpdrvAtExit);
				s_atexit_installed = true;
			}
		}
	}

	return ret;
}

NNPError nnpdrvCreateInferContext(uint32_t deviceNum,
				  NNPInferContext *outContext)
{
	return nnpdrvCreateInferContextWithFlags(deviceNum, 0, outContext);
}

NNPError nnpdrvDestroyInferContext(NNPInferContext ctx)
{
	NNPError ret;

	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	ret = c->destroy();
	if (ret == NNP_NO_ERROR)
		s_contexts.remove(ctx);

	return ret;
}

NNPError nnpdrvRecoverInferContext(NNPInferContext ctx)
{
	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	return c->recover();
}

NNPError nnpdrvQueryInferContextInfo(NNPInferContext 	  ctx,
                                     NNPInferContextInfo *outInferContextInfo)
{
	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	if (!outInferContextInfo)
		return NNP_INVALID_ARGUMENT;

	outInferContextInfo->deviceNum = c->device()->number();
	outInferContextInfo->contextId = c->chan()->id();

	return NNP_NO_ERROR;
}

NNPError nnpdrvInferContextTraceUserData(NNPInferContext ctx,
					 const char     *key,
					 uint64_t        user_data)
{
	if (strlen(key) == 0)
		return NNP_INVALID_ARGUMENT;

	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	return c->trace_user_data(key, user_data);
}

NNPError nnpdrvCreateHostResource(uint64_t         byteSize,
				  uint32_t         usageFlags,
				  NNPHostResource *outHostRes)
{
	nnpiHostRes::ptr hostres;
	int rc;

	if (!outHostRes)
		return NNP_INVALID_ARGUMENT;

	if ((usageFlags & NNP_RESOURCE_USAGE_NETWORK) != 0)
		return NNP_NOT_SUPPORTED;

	rc = nnpiHostRes::create(byteSize,
				 usageFlags,
				 hostres);
	if (rc != 0) {
		switch (rc) {
		case ENODEV:
			return NNP_NO_SUCH_DEVICE;
		case ENOMEM:
			return NNP_OUT_OF_MEMORY;
		case EINVAL:
			return NNP_INVALID_ARGUMENT;
		default:
			return NNP_IO_ERROR;
		}
	}

	*outHostRes = nnpiHostRes::handle_map.makeHandle(hostres);
	hostres->set_user_hdl(*outHostRes);
	return NNP_NO_ERROR;
}

NNPError nnpdrvCreateDmaBufHostResource(int              dmaBuf,
					uint32_t         usageFlags,
					NNPHostResource *outHostRes)
{
	return NNP_NOT_SUPPORTED;
}

NNPError nnpdrvCreateHostResourceFromBuf(const void      *buf,
					 uint64_t         byteSize,
					 uint32_t         usageFlags,
					 NNPHostResource *outHostRes)
{
	nnpiHostRes::ptr hostres;
	int rc;

	if (!outHostRes || !buf)
		return NNP_INVALID_ARGUMENT;

	if ((usageFlags & NNP_RESOURCE_USAGE_NETWORK) != 0)
		return NNP_NOT_SUPPORTED;

	rc = nnpiHostRes::createFromBuf(buf,
					byteSize,
					usageFlags,
					hostres);

	if (rc != 0) {
		switch (rc) {
		case ENODEV:
			return NNP_NO_SUCH_DEVICE;
		case ENOMEM:
			return NNP_OUT_OF_MEMORY;
		case EINVAL:
			return NNP_INVALID_ARGUMENT;
		default:
			return NNP_IO_ERROR;
		}
	}

	*outHostRes = nnpiHostRes::handle_map.makeHandle(hostres);
	hostres->set_user_hdl(*outHostRes);
	return NNP_NO_ERROR;
}

NNPError nnpdrvDestroyHostResource(NNPHostResource hostRes)
{
	if (!nnpiHostRes::handle_map.remove(hostRes))
		return NNP_NO_SUCH_RESOURCE;

	return NNP_NO_ERROR;
}

NNPError nnpdrvGetHostResourceCPUAddress(NNPHostResource hostRes, void **outPtr)
{
	nnpiHostRes::ptr hostres = nnpiHostRes::handle_map.find(hostRes);
	if (!hostres.get())
		return NNP_NO_SUCH_RESOURCE;

	*outPtr = hostres->vaddr();

	return NNP_NO_ERROR;
}

NNPError nnpdrvGetHostResourceDmaBufFD(NNPHostResource hostRes, int *outFD)
{
	nnpiHostRes::ptr hostres = nnpiHostRes::handle_map.find(hostRes);
	if (!hostres.get())
		return NNP_NO_SUCH_RESOURCE;

	*outFD = hostres->dmaBuf_fd();

	return NNP_NO_ERROR;
}

NNPError nnpdrvLockHostResource(NNPHostResource hostRes,
				uint32_t        timeoutUs)
{
	nnpiHostRes::ptr hostres = nnpiHostRes::handle_map.find(hostRes);
	if (!hostres.get())
		return NNP_NO_SUCH_RESOURCE;

	bool for_write = (hostres->usageFlags() & (NNP_RESOURCE_USAGE_NN_INPUT | NNP_RESOURCE_USAGE_NN_OUTPUT)) !=
			 NNP_RESOURCE_USAGE_NN_OUTPUT;

	return hostres->lock_cpu_access(timeoutUs, for_write);
}

NNPError nnpdrvUnlockHostResource(NNPHostResource hostRes)
{
	nnpiHostRes::ptr hostres = nnpiHostRes::handle_map.find(hostRes);
	if (!hostres.get())
		return NNP_NO_SUCH_RESOURCE;

	return hostres->unlock_cpu_access();
}

NNPError nnpdrvCreateDeviceResourceFIFO(NNPInferContext    ctx,
					uint64_t           elemByteSize,
					uint32_t           depth,
					uint64_t           align,
					uint32_t           usageFlags,
					NNPDeviceResource *outDevRes)
{
	nnpiDevRes::ptr devres;
	NNPError ret;

	if (!outDevRes)
		return NNP_INVALID_ARGUMENT;

	if (usageFlags & NNP_RESOURECE_USAGE_LOCKLESS)
		return NNP_INVALID_ARGUMENT;

	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	ret = nnpiDevRes::create(c,
				 elemByteSize,
				 depth,
				 align,
				 usageFlags,
				 devres);
	if (ret != NNP_NO_ERROR)
		return ret;

	*outDevRes = s_devres.makeHandle(devres);
	devres->set_user_hdl(*outDevRes);
	devres->m_ctx->send_user_handle(INF_OBJ_TYPE_DEVRES, devres->id(), 0, *outDevRes);

	return NNP_NO_ERROR;
}

NNPError nnpdrvCreateDeviceResource(NNPInferContext    ctx,
				    uint64_t           byteSize,
				    uint64_t           align,
				    uint32_t           usageFlags,
				    NNPDeviceResource *outDevRes)
{
	return nnpdrvCreateDeviceResourceFIFO(ctx,
					      byteSize,
					      1,
					      align,
					      usageFlags,
					      outDevRes);
}


NNPError nnpdrvDestroyDeviceResource(NNPDeviceResource devRes)
{
	NNPError ret;
	nnpiDevRes::ptr d = s_devres.find(devRes);
	if (!d.get())
		return NNP_NO_SUCH_RESOURCE;

	ret = d->destroy();
	if (ret == NNP_NO_ERROR)
		s_devres.remove(devRes);

	return ret;
}

NNPError nnpdrvMarkDeviceResourceDirty(NNPDeviceResource devRes)
{
	nnpiDevRes::ptr d = s_devres.find(devRes);
	if (!d.get())
		return NNP_NO_SUCH_RESOURCE;

	return d->markDirty();
}

static NNPError createCopyCommand(NNPInferContext ctx,
				  NNPHostResource hostRes,
				  NNPDeviceResource devRes,
				  bool              is_c2h,
				  NNPCopyHandle    *outHandle)
{
	NNPError ret;
	nnpiCopyCommand::ptr copy;

	if (!outHandle)
		return NNP_INVALID_ARGUMENT;

	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	nnpiHostRes::ptr hostres = nnpiHostRes::handle_map.find(hostRes);
	if (!hostres.get())
		return NNP_NO_SUCH_RESOURCE;

	nnpiDevRes::ptr devres = s_devres.find(devRes);
	if (!devres.get())
		return NNP_NO_SUCH_RESOURCE;

	if (devres->m_ctx != c)
		return NNP_NO_SUCH_RESOURCE;

	ret = nnpiCopyCommand::create(c,
				      devres,
				      hostres,
				      is_c2h,
				      copy);
	if (ret == NNP_NO_ERROR) {
		*outHandle = s_copy.makeHandle(copy);
		copy->set_user_hdl(*outHandle);
		copy->context()->send_user_handle(INF_OBJ_TYPE_COPY, copy->id(), COPY_USER_HANDLE_TYPE_COPY, *outHandle);
	}

	return ret;
}

static NNPError createDeviceToDeviceCopyCommand(NNPInferContext ctx,
						NNPDeviceResource dstDevResHandle,
						NNPDeviceResource srcDevResHandle,
						NNPCopyHandle *outHandle)
{
	NNPError ret;
	nnpiCopyCommand::ptr copy;
	nnpiDevRes::ptr src_devres;
	nnpiDevRes::ptr dst_devres;

	if (!outHandle)
		return NNP_INVALID_ARGUMENT;

	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	src_devres = s_devres.find(srcDevResHandle);
	if (!src_devres.get())
		return NNP_NO_SUCH_RESOURCE;

	dst_devres = s_devres.find(dstDevResHandle);
	if (!dst_devres.get())
		return NNP_NO_SUCH_RESOURCE;

	ret = nnpiCopyCommand::create_d2d(c,
					  dst_devres,
					  src_devres,
					  copy);
	if (ret == NNP_NO_ERROR) {
		*outHandle = s_copy.makeHandle(copy);
		copy->set_user_hdl(*outHandle);
		copy->context()->send_user_handle(INF_OBJ_TYPE_COPY, copy->id(), COPY_USER_HANDLE_TYPE_COPY, *outHandle);
	}

	return ret;
}

NNPError nnpdrvCreateHostToDeviceCopyHandle(NNPInferContext ctx, NNPHostResource hostRes, NNPDeviceResource devRes, NNPCopyHandle *outHandle)
{
	return createCopyCommand(ctx,
				 hostRes,
				 devRes,
				 false,
				 outHandle);
}

NNPError nnpdrvCreateDeviceToHostCopyHandle(NNPInferContext ctx, NNPDeviceResource devRes, NNPHostResource hostRes, NNPCopyHandle *outHandle)
{
	return createCopyCommand(ctx,
				 hostRes,
				 devRes,
				 true,
				 outHandle);
}

NNPError nnpdrvCreateDeviceToDeviceCopyHandle(NNPInferContext ctx, NNPDeviceResource to, NNPDeviceResource from, NNPCopyHandle *outHandle)
{
	return createDeviceToDeviceCopyCommand(ctx,to,from, outHandle);
}

NNPError nnpdrvDestroyCopyHandle(NNPCopyHandle   copyHandle)
{
	NNPError ret;
	nnpiCopyCommand::ptr copy = s_copy.find(copyHandle);
	if (!copy.get())
		return NNP_NO_SUCH_COPY_HANDLE;

	ret = copy->destroy();
	if (ret == NNP_NO_ERROR)
		s_copy.remove(copyHandle);

	return ret;
}

static uint32_t calcOptimalBlockSize(uint64_t size)
{
#define MAX_BLOCK_SIZE 0x10000 //65KB

	uint32_t opt_size = NNP_PAGE_SIZE;

	while (opt_size < size && opt_size < MAX_BLOCK_SIZE)
		opt_size <<= 1;

	return opt_size;
}

NNPError nnpdrvDeviceResourceSubLoadFromStream(NNPDeviceResource devRes,
					       uint64_t          offset,
					       NNPStreamReadCb   read_cb,
					       void             *stream_ctx)
{
	const uint32_t nblocks = 2;
	uint32_t block_size;

	nnpiDevRes::ptr devres = s_devres.find(devRes);
	if (!devres.get())
		return NNP_NO_SUCH_RESOURCE;

	block_size = calcOptimalBlockSize(devres->size() - offset);

	nnpiDevice::ptr dev(devres->m_ctx->device());
	nnpiHostRes::ptr hostres[nblocks];
	uint16_t         hostres_map_id[nblocks];
	bool             is_mapped[nblocks];
	nnpiCopyCommand::ptr copy[nblocks];
	uint32_t         block_idx = 0;
	uint64_t         devres_offset = offset;
	int              rc;
	NNPError         ret = NNP_NO_ERROR;
	ssize_t          n;

	for (uint32_t i = 0; i < nblocks; i++)
		is_mapped[i] = false;

	do {
		if (hostres[block_idx].get() == NULL) {
			rc = nnpiHostRes::create(block_size,
						 NNP_RESOURCE_USAGE_NN_INPUT,
						 hostres[block_idx]);
			if (rc != 0) {
				switch (rc) {
				case ENODEV:
					ret = NNP_NO_SUCH_DEVICE;
				case ENOMEM:
					ret = NNP_OUT_OF_MEMORY;
				case EINVAL:
					ret = NNP_INVALID_ARGUMENT;
				default:
					ret = NNP_IO_ERROR;
				}

				break;
			}

			rc = dev->mapHostResource(devres->m_ctx->chan()->id(),
						  hostres[block_idx],
						  hostres_map_id[block_idx]);
			if (rc != 0) {
				ret = nnpiDevice::errnoToNNPError(rc);
				break;
			}

			is_mapped[block_idx] = true;

			ret = nnpiCopyCommand::create_subres(devres,
							     copy[block_idx]);
			if (ret != NNP_NO_ERROR)
				break;
		}

		ret = hostres[block_idx]->lock_cpu_access(UINT32_MAX, true);
		if (ret != NNP_NO_ERROR)
			break;

		n = read_cb(stream_ctx,
			    hostres[block_idx]->vaddr(),
			    block_size);
		ret = hostres[block_idx]->unlock_cpu_access();
		if (n < 0) {
			ret = NNP_IO_ERROR;
			break;
		} else if (ret != NNP_NO_ERROR) {
			break;
		} else if (n > 0) {
			ret = copy[block_idx]->schedule(hostres[block_idx],
							hostres_map_id[block_idx],
							devres_offset,
							n);
			if (ret != NNP_NO_ERROR)
				break;

			devres_offset += n;
			block_idx = (block_idx + 1) % nblocks;
		}
	} while (n == block_size);

	if (ret == NNP_NO_ERROR) {
		/* wait for all copy operations to complete */
		for (uint32_t i = 0; i < nblocks; i++) {
			if (hostres[i].get() != NULL) {
				hostres[i]->lock_cpu_access(UINT32_MAX, true);
				hostres[i]->unlock_cpu_access();
			}
		}
	}

	for (uint32_t i = 0; i < nblocks; i++) {
		if (hostres[i].get() != NULL && is_mapped[i]) {
			dev->unmapHostResource(devres->m_ctx->chan()->id(),
					       hostres_map_id[i]);
		}

		if (copy[i].get() != NULL)
			copy[i]->destroy();
	}

	return ret;
}

static ssize_t file_stream_read_cb(void    *stream_ctx,
				   void    *dst,
				   size_t   size)
{
	FILE *fp = (FILE *)stream_ctx;
	size_t n;

	n = fread(dst, 1, size, fp);

	if (n == 0 && !feof(fp))
		return -1;

	return n;
}

NNPError nnpdrvCreateDeviceResourceFromFile(NNPInferContext    ctx,
					    const char        *fileName,
						uint64_t           align,
					    uint32_t           usageFlags,
					    NNPDeviceResource *out_devRes)
{
	int ret;
	uint64_t fileSize;
	FILE *fileHandle;
	struct stat fileStat;
	NNPError nnp_err;

	if (!fileName)
		return NNP_INVALID_ARGUMENT;

	/* a network blob resource cannot be input or output resource */
	if ((usageFlags & NNP_RESOURCE_USAGE_NETWORK) &&
	    usageFlags != NNP_RESOURCE_USAGE_NETWORK)
		return NNP_INVALID_ARGUMENT;

	ret = stat(fileName, &fileStat);
	if (ret < 0)
		return nnpiDevice::errnoToNNPError(errno);

	fileSize = fileStat.st_size;

	fileHandle = fopen(fileName, "r");
	if (fileHandle == NULL)
		return nnpiDevice::errnoToNNPError(errno);

	nnp_err = nnpdrvCreateDeviceResource(ctx,
					     fileSize,
					     align,
					     usageFlags,
					     out_devRes);
	if (nnp_err != NNP_NO_ERROR) {
		fclose(fileHandle);
		return nnp_err;
	}

	nnp_err = nnpdrvDeviceResourceSubLoadFromStream(*out_devRes,
							0,
							file_stream_read_cb,
							fileHandle);

	fclose(fileHandle);

	return nnp_err;
}

struct buf_stream {
	const char *buffer;
	uint64_t    bufferSize;
	uint64_t    pos;
};

static ssize_t buf_stream_read_cb(void    *stream_ctx,
				  void    *dst,
				  size_t   size)
{
	struct buf_stream *stream = (struct buf_stream *)stream_ctx;
	size_t n;

	if (!stream)
		return -1;

	if (stream->pos >= stream->bufferSize)
		return 0;

	n = stream->bufferSize - stream->pos;
	if (n > size)
		n = size;

	if (n > 0) {
		memcpy_s(dst, size, stream->buffer + stream->pos, n);
		stream->pos += n;
	}

	return n;
}

NNPError nnpdrvDeviceResourceSubLoad(NNPDeviceResource devRes,
				     uint64_t          offset,
				     const void        *data,
				     uint64_t          dataSize)
{
	struct buf_stream stream;
	NNPError nnp_err = NNP_NO_ERROR;

	if (!data)
		return NNP_INVALID_ARGUMENT;

	if (dataSize == 0)
		return NNP_NO_ERROR;

	stream.buffer = (const char *)data;
	stream.bufferSize = dataSize;
	stream.pos = 0;

	nnp_err = nnpdrvDeviceResourceSubLoadFromStream(devRes,
							offset,
							buf_stream_read_cb,
							&stream);

	return nnp_err;
}

NNPError nnpdrvCreateDeviceNetwork(NNPInferContext ctx,
				   const char *netBlobFilename,
				   void *netConfigData,
				   uint32_t netConfigDataSize,
				   NNPDeviceNetwork *outNetHandle)
{
	NNPDeviceResource devRes[2];
	char *data_fileName;
	bool hasDataDevRes = false;
	size_t name_len;
	uint32_t n_devres = 0;
	NNPError nnp_err;
	unsigned int i;

	if (outNetHandle == NULL || netBlobFilename == NULL)
		return NNP_INVALID_ARGUMENT;

	if (netConfigDataSize > 0 && netConfigData == NULL)
		return NNP_INVALID_ARGUMENT;

	nnp_err = nnpdrvCreateDeviceResourceFromFile(ctx,
						     netBlobFilename,
							 0,
						     NNP_RESOURCE_USAGE_NETWORK,
						     &devRes[0]);

	if (nnp_err != NNP_NO_ERROR)
		return nnp_err;

	n_devres++;

	/*
	 * If filename ends in .xml and there is same file with extension
	 * .data.xml, need to load that file as well into another resource
	 */
	name_len = strlen(netBlobFilename);
	if (name_len >= 4)
		if (!strcmp(&netBlobFilename[name_len - 4], ".xml")) {
			data_fileName = (char *)malloc(name_len + 6);
			if (data_fileName == NULL) {
				nnp_err = NNP_OUT_OF_MEMORY;
				goto finish;
			}

			strcpy_s(data_fileName, name_len + 6, netBlobFilename);
			strcpy_s(&data_fileName[name_len - 4], 10, ".data.xml");
			nnp_err = nnpdrvCreateDeviceResourceFromFile(ctx,
						(const char *)data_fileName,
						0,
						NNP_RESOURCE_USAGE_NETWORK,
						&devRes[1]);

			hasDataDevRes = (nnp_err == NNP_NO_ERROR);
			if (hasDataDevRes)
				n_devres++;

			free(data_fileName);
		}

	nnp_err = nnpdrvCreateDeviceNetworkWithResources(ctx,
							 devRes,
							 n_devres,
							 netConfigData,
							 netConfigDataSize,
							 outNetHandle);

finish:
	for (i = 0; i < n_devres; i++)
		nnpdrvDestroyDeviceResource(devRes[i]);

	return nnp_err;
}

static NNPError get_devres_array(NNPDeviceResource *devResArray,
				 uint32_t           devResArrayLen,
				 nnpiDevRes::vec   &out_vec)
{
	out_vec.clear();

	for (uint32_t i = 0; i < devResArrayLen; i++) {
		nnpiDevRes::ptr devres = s_devres.find(devResArray[i]);
		if (!devres.get())
			return NNP_NO_SUCH_RESOURCE;

		if (!(devres->usageFlags() & NNP_RESOURCE_USAGE_NETWORK))
			return NNP_INCOMPATIBLE_RESOURCES;

		out_vec.push_back(devres);
	}

	return NNP_NO_ERROR;
}

NNPError nnpdrvCreateDeviceNetworkWithResources(NNPInferContext    ctx,
						NNPDeviceResource *devResArray,
						uint32_t           devResArrayLen,
						void              *netConfigData,
						uint32_t           netConfigDataSize,
						NNPDeviceNetwork  *outNetHandle)
{
	nnpiDevRes::vec res_vec;
	nnpiDevNet::ptr devnet;
	NNPError ret;

	if (!outNetHandle)
		return NNP_INVALID_ARGUMENT;

	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	ret = get_devres_array(devResArray,
			       devResArrayLen,
			       res_vec);
	if (ret != NNP_NO_ERROR)
		return ret;

	ret = nnpiDevNet::create(c,
				 res_vec,
				 netConfigData,
				 netConfigDataSize,
				 devnet);
	if (ret == NNP_NO_ERROR) {
		*outNetHandle = s_networks.makeHandle(devnet);
		devnet->set_user_hdl(*outNetHandle);
		devnet->context()->send_user_handle(INF_OBJ_TYPE_DEVNET, devnet->id(), 0, *outNetHandle);
	}

	return ret;
}

NNPError nnpdrvDeviceNetworkAddResources(NNPDeviceNetwork   devNet,
					 NNPDeviceResource *devResArray,
					 uint32_t           devResArrayLen,
					 void              *configData,
					 uint32_t           configDataSize)
{
	nnpiDevRes::vec res_vec;
	NNPError ret;

	nnpiDevNet::ptr devent = s_networks.find(devNet);
	if (!devent.get())
		return NNP_NO_SUCH_NETWORK;

	ret = get_devres_array(devResArray,
			       devResArrayLen,
			       res_vec);
	if (ret != NNP_NO_ERROR)
		return ret;

	ret = devent->add_resources(res_vec,
				    configData,
				    configDataSize);

	return ret;
}

NNPError nnpdrvRuntimeControl(NNPDeviceNetwork   devNet,
			      void              *ioBuffer,
			      uint32_t          *ioBufferSize,
			      NNPDeviceResource *ioDevResArray,
			      uint32_t          *ioDevResArrayLen)
{
	return NNP_IO_ERROR;
}

NNPError nnpdrvUpdateDeviceNetwork(NNPDeviceNetwork devNet,
				   const char	   *netBlobFilename,
				   void            *netConfigData,
				   uint32_t         netConfigDataSize)
{
	return NNP_NO_ERROR;
}

NNPError nnpdrvDestroyDeviceNetwork(NNPDeviceNetwork devNet)
{
	nnpiDevNet::ptr devnet = s_networks.find(devNet);
	if (!devnet.get())
		return NNP_NO_SUCH_NETWORK;

	NNPError ret = devnet->destroy();
	if (ret == NNP_NO_ERROR)
		s_networks.remove(devNet);

	return ret;
}

NNPError nnpdrvDeviceNetworkReserveExecResources(NNPDeviceNetwork devNet, uint32_t timeoutUs)
{
	nnpiDevNet::ptr devnet = s_networks.find(devNet);
	if (!devnet.get())
		return NNP_NO_SUCH_NETWORK;

	return devnet->setProperty(NNP_NETWORK_RESERVATION, true, timeoutUs);
}

NNPError nnpdrvDeviceNetworkReleaseExecResources(NNPDeviceNetwork devNet)
{
	nnpiDevNet::ptr devnet = s_networks.find(devNet);
	if (!devnet.get())
		return NNP_NO_SUCH_NETWORK;

	return devnet->setProperty(NNP_NETWORK_RESERVATION, false, 0);
}

NNPError nnpdrvDeviceSetNetworkProperty(NNPDeviceNetwork devNet, NNPNetPropertiesType property, uint32_t property_val, uint32_t timeoutUs)
{
	nnpiDevNet::ptr devnet = s_networks.find(devNet);
	if (!devnet.get())
		return NNP_NO_SUCH_NETWORK;

	return devnet->setProperty(property, property_val, timeoutUs);
}

NNPError nnpdrvCreateInferRequest(NNPDeviceNetwork   devNet,
				  void              *configData,
				  uint32_t           configDataSize,
				  uint32_t           maxExecConfigSize,
				  uint32_t           numInputs,
				  NNPDeviceResource *inputDevResources,
				  uint32_t           numOutputs,
				  NNPDeviceResource *outputDevResources,
				  NNPInferRequest   *outHandle)
{
	if ((configDataSize > 0 && configData == NULL) ||
	    (numInputs > 0 && inputDevResources == NULL) ||
	    (numOutputs > 0 && outputDevResources == NULL) ||
	    outHandle == NULL)
		return NNP_INVALID_ARGUMENT;

	nnpiDevNet::ptr devnet = s_networks.find(devNet);
	if (!devnet.get())
		return NNP_NO_SUCH_NETWORK;

	NNPError ret;
	nnpiInfReq::ptr infreq;
	nnpiDevRes::vec inputs;
	nnpiDevRes::vec outputs;

	for (uint32_t i = 0; i < numInputs; i++) {
		nnpiDevRes::ptr devres = s_devres.find(inputDevResources[i]);
		if (!devres.get())
			return NNP_NO_SUCH_RESOURCE;
		inputs.push_back(devres);
	}

	for (uint32_t i = 0; i < numOutputs; i++) {
		nnpiDevRes::ptr devres = s_devres.find(outputDevResources[i]);
		if (!devres.get())
			return NNP_NO_SUCH_RESOURCE;
		outputs.push_back(devres);
	}

	ret = nnpiInfReq::create(devnet,
				 inputs,
				 outputs,
				 configData,
				 configDataSize,
				 infreq);

	if (ret == NNP_NO_ERROR) {
		*outHandle = s_infreqs.makeHandle(infreq);
		infreq->set_user_hdl(*outHandle);
		devnet->context()->send_user_handle(INF_OBJ_TYPE_INFREQ, devnet->id(), infreq->id(), *outHandle);
	}

	return ret;
}

NNPError nnpdrvDestroyInferRequest(NNPInferRequest inferReq)
{
	nnpiInfReq::ptr infreq;

	infreq = s_infreqs.find(inferReq);
	if (!infreq.get())
		return NNP_NO_SUCH_INFREQ_HANDLE;

	infreq->destroy();
	s_infreqs.remove(inferReq);

	return NNP_NO_ERROR;
}

NNPError nnpdrvScheduleInferReq(NNPInferRequest infReq,
				nnpdrvinfSchedParams *schedParams)
{
	nnpiInfReq::ptr infreq;

	infreq = s_infreqs.find(infReq);
	if (!infreq.get())
		return NNP_NO_SUCH_INFREQ_HANDLE;

	return infreq->schedule(schedParams);
}

NNPError nnpdrvScheduleCopy(NNPCopyHandle copyHandle, uint64_t byteSize, uint8_t priority)
{
	nnpiCopyCommand::ptr copy = s_copy.find(copyHandle);
	if (!copy.get())
		return NNP_NO_SUCH_COPY_HANDLE;

	return copy->schedule(byteSize, priority);
}

NNPError nnpdrvGetMarker(NNPInferContext  ctx,
			 NNPMarker       *outMarker)
{
	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	if (c->broken() && !c->aborted())
		return NNP_CONTEXT_BROKEN;

	uint32_t marker;

	NNPError ret = c->createMarker(marker);

	if (ret == NNP_NO_ERROR)
		*outMarker = (NNPMarker)marker;

	return ret;
}

NNPError nnpdrvFinish(NNPInferContext ctx)
{
	NNPError ret;
	NNPMarker marker = 0;

	ret = nnpdrvGetMarker(ctx, &marker);
	if (ret != NNP_NO_ERROR)
		return ret;

	ret = nnpdrvWaitForMarker(ctx, marker, UINT32_MAX);

	return ret;
}

NNPError nnpdrvWaitForMarker(NNPInferContext ctx,
			     NNPMarker       marker,
			     uint32_t        timeoutUs)
{
	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	return c->waitMarker((uint32_t)marker, timeoutUs);
}

NNPError nnpdrvGetError(NNPInferContext		ctx,
			NNPCriticalErrorInfo	*outErrorInfo)
{
	return nnpdrvWaitForCriticalError(ctx, 0, outErrorInfo);
}

NNPError nnpdrvWaitForCriticalError(NNPInferContext ctx,
				    uint32_t        timeoutUs,
				    NNPCriticalErrorInfo *outErrorInfo)
{
	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	return c->waitCriticalError(outErrorInfo, timeoutUs);
}

NNPError nnpdrvGetCriticalErrorMessage(NNPInferContext ctx,
				       void          *buf,
				       uint32_t       buf_size,
				       uint32_t      *out_buf_size)
{
	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	return c->errorList().getErrorMessage(0, buf, buf_size, out_buf_size);
}

NNPError nnpdrvGetCopyContext(NNPCopyHandle    copyHandle,
			      NNPInferContext *outCtx)
{
	if (!outCtx)
		return NNP_INVALID_ARGUMENT;

	nnpiCopyCommand::ptr copy = s_copy.find(copyHandle);
	if (!copy.get())
		return NNP_NO_SUCH_COPY_HANDLE;

	*outCtx = copy->context()->user_hdl();

	return NNP_NO_ERROR;
}

NNPError nnpdrvGetInferReqContext(NNPInferRequest  infReq,
				  NNPInferContext *outCtx)
{
	nnpiInfReq::ptr infreq;

	if (!outCtx)
		return NNP_INVALID_ARGUMENT;

	infreq = s_infreqs.find(infReq);
	if (!infreq.get())
		return NNP_NO_SUCH_INFREQ_HANDLE;

	*outCtx = infreq->network()->context()->user_hdl();

	return NNP_NO_ERROR;
}

NNPError nnpdrvCreateCommandListBegin(NNPInferContext  ctx,
				      NNPCommandList  *outCommandList)
{
	nnpiCommandList::ptr cmdlist;

	nnpiInfContext::ptr c = s_contexts.find(ctx);
	if (!c.get())
		return NNP_NO_SUCH_CONTEXT;

	NNPError ret = nnpiCommandList::create(c, cmdlist);
	if (ret != NNP_NO_ERROR)
		return ret;

	*outCommandList = s_cmdlists.makeHandle(cmdlist);
	cmdlist->set_user_hdl(*outCommandList);

	return NNP_NO_ERROR;
}

NNPError nnpdrvCreateCommandListEnd(NNPCommandList commandList)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	uint32_t optFlags = nnpiCommandList::BATCH_COPIES;

	if (getenv("NNPI_NO_BATCH_COPIES"))
		optFlags &= ~(nnpiCommandList::BATCH_COPIES);

	return cmdlist->finalize(optFlags);
}

NNPError nnpdrvDestroyCommandList(NNPCommandList commandList)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	NNPError ret = cmdlist->destroy();
	if (ret == NNP_NO_ERROR)
		s_cmdlists.remove(commandList);

	return ret;
}

NNPError nnpdrvCommandListAppendCopy(NNPCommandList        commandList,
				     NNPCopyHandle         copyHandle,
				     uint64_t              byteSize,
				     uint8_t               priority,
				     uint32_t              flags)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	nnpiCopyCommand::ptr copy = s_copy.find(copyHandle);
	if (!copy.get())
		return NNP_NO_SUCH_COPY_HANDLE;

	if (byteSize == 0)
		byteSize = UINT64_MAX;
	if ((flags & NNP_SCHEDULE_SKIP_EXECUTION) != 0)
		byteSize = 0;
	nnpiInfCopyCommandSchedParams *copy_params = new nnpiInfCopyCommandSchedParams(copy,
										       priority,
										       byteSize);

	return cmdlist->append(copy_params);
}

NNPError nnpdrvCommandListAppendInferRequest(NNPCommandList        commandList,
					     NNPInferRequest       infReq,
					     nnpdrvinfSchedParams *schedParams)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	nnpiInfReq::ptr infreq = s_infreqs.find(infReq);
	if (!infreq.get())
		return NNP_NO_SUCH_INFREQ_HANDLE;

	nnpiInfReqSchedParams *inf_params = new nnpiInfReqSchedParams(infreq,
								      schedParams);

	return cmdlist->append(inf_params);
}

NNPError nnpdrvCommandListOverwriteCopy(NNPCommandList        commandList,
					uint16_t              copy_idx,
					uint64_t              byteSize,
					uint8_t               priority,
					uint32_t              flags)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	nnpiInfCommandSchedParams *command = cmdlist->get_cmd_for_overwrite(copy_idx);
	if (command == NULL)
		return NNP_INVALID_ARGUMENT;

	if (command->type() != CMDLIST_CMD_COPY)
		return NNP_NO_SUCH_COPY_HANDLE;

	nnpiInfCopyCommandSchedParams *copy_params =
		dynamic_cast<nnpiInfCopyCommandSchedParams *> (command);

	if (byteSize == 0)
		byteSize = UINT64_MAX;
	if ((flags & NNP_SCHEDULE_SKIP_EXECUTION) != 0)
		byteSize = 0;
	copy_params->overwrite(priority, byteSize);

	return NNP_NO_ERROR;
}

NNPError nnpdrvCommandListOverwriteInferRequest(NNPCommandList        commandList,
						uint16_t              infreq_idx,
						nnpdrvinfSchedParams *schedParams)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	nnpiInfCommandSchedParams *command = cmdlist->get_cmd_for_overwrite(infreq_idx);
	if (command == NULL)
		return NNP_INVALID_ARGUMENT;

	if (command->type() != CMDLIST_CMD_INFREQ)
		return NNP_NO_SUCH_INFREQ_HANDLE;

	nnpiInfReqSchedParams *infreq_params =
		dynamic_cast<nnpiInfReqSchedParams *> (command);
	infreq_params->overwrite(schedParams);

	return NNP_NO_ERROR;
}

NNPError nnpdrvScheduleCommandList(NNPCommandList commandList)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	return cmdlist->schedule();
}

NNPError nnpdrvWaitCommandList(NNPCommandList commandList,
			       uint32_t timeoutUs,
			       NNPCriticalErrorInfo *errors,
			       uint32_t *numErrors)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	return cmdlist->wait(timeoutUs,
			     errors,
			     numErrors);
}

NNPError nnpdrvCommandListGetErrorMessage(NNPCommandList commandList,
					  uint32_t       index,
					  void          *buf,
					  uint32_t       buf_size,
					  uint32_t      *out_buf_size)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	return cmdlist->getErrorList()->getErrorMessage(index, buf, buf_size, out_buf_size);
}

NNPError nnpdrvCommandListClearErrorState(NNPCommandList commandList)
{
	nnpiCommandList::ptr cmdlist = s_cmdlists.find(commandList);
	if (!cmdlist.get())
		return NNP_NO_SUCH_CMDLIST;

	return cmdlist->clearErrors();
}

void nnpiInferenceLock(void)
{
	s_contexts.mutex().lock();

	//nnpiGlobalLock must be locked
	nnpiActiveContexts::lock();

	nnpiHostRes::handle_map.mutex().lock();
	s_devres.mutex().lock();
	s_networks.mutex().lock();
	s_infreqs.mutex().lock();
	s_copy.mutex().lock();
	s_cmdlists.mutex().lock();
}

void nnpiInferenceUnlock(void)
{
	s_cmdlists.mutex().unlock();
	s_copy.mutex().unlock();
	s_infreqs.mutex().unlock();
	s_networks.mutex().unlock();
	s_devres.mutex().unlock();
	nnpiHostRes::handle_map.mutex().unlock();

	//nnpiGlobalLock must be locked
	nnpiActiveContexts::unlock();

	s_contexts.mutex().unlock();
}

void nnpiForkChildInferenceReset(void)
{
	nnpiActiveContexts::close_all();

	s_cmdlists.clear();
	s_copy.clear();
	s_infreqs.clear();
	s_networks.clear();
	s_devres.clear();
	nnpiHostRes::handle_map.clear();
	s_contexts.clear();

	nnpiActiveContexts::destroy();
}

#ifdef ULT
NNPError ult_inference_copy_fail(NNPInferContext ctx, NNPHostResource hostRes, NNPDeviceResource devRes, bool fail_sched, NNPCopyHandle *outHandle)
{
	return NNP_IO_ERROR;
}

int ult_inference_copy_fail_cleanup(NNPInferContext ctx)
{
	return -1;
}
#endif
