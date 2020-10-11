/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiInfContext.h"
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <set>
#include "ipc_chan_protocol.h"
#include "ipc_c2h_events.h"
#include "nnpiContextObjDB.h"
#include "nnp_log.h"
#include <inttypes.h>
#include "safe_lib.h"

static const uint32_t H2C_RINGBUF_SIZE = 2 * NNP_PAGE_SIZE;
static const uint32_t C2H_RINGBUF_SIZE = 2 * NNP_PAGE_SIZE;


NNPError event_valToNNPError(uint32_t event_val)
{
	switch (event_val) {
	case 0:
		return NNP_NO_ERROR;
	case NNP_IPC_NO_SUCH_CONTEXT:
		return NNP_NO_SUCH_CONTEXT;
	case NNP_IPC_NO_SUCH_DEVRES:
		return NNP_NO_SUCH_RESOURCE;
	case NNP_IPC_NO_SUCH_COPY:
		return NNP_NO_SUCH_COPY_HANDLE;
	case NNP_IPC_NO_SUCH_NET:
		return NNP_NO_SUCH_NETWORK;
	case NNP_IPC_NO_SUCH_INFREQ:
		return NNP_NO_SUCH_INFREQ_HANDLE;
	case NNP_IPC_NO_DAEMON:
		return NNP_DEVICE_NOT_READY;
	case NNP_IPC_NO_MEMORY:
		return NNP_OUT_OF_MEMORY;
	case NNP_IPC_RUNTIME_NOT_SUPPORTED:
		return NNP_NOT_SUPPORTED;
	case NNP_IPC_RUNTIME_INVALID_EXECUTABLE_NETWORK_BINARY:
		return NNP_INVALID_EXECUTABLE_NETWORK_BINARY;
	case NNP_IPC_RUNTIME_INFER_MISSING_RESOURCE:
		return NNP_INFER_MISSING_RESOURCE;
	case NNP_IPC_DEVNET_RESERVE_INSUFFICIENT_RESOURCES:
		return NNP_DEVNET_RESERVE_INSUFFICIENT_RESOURCES;
	case NNP_IPC_TIMEOUT_EXCEEDED:
		return NNP_TIMED_OUT;
	case NNP_IPC_ECC_ALLOC_FAILED:
		return NNP_OUT_OF_ECC_MEMORY;

	case NNP_IPC_CONTEXT_BROKEN:
		return NNP_CONTEXT_BROKEN;

	case NNP_IPC_RUNTIME_LAUNCH_FAILED:
	case NNP_IPC_RUNTIME_FAILED:
	case NNP_IPC_ALREADY_EXIST:
	case NNP_IPC_DMA_ERROR:
	case NNP_IPC_RUNTIME_INFER_EXEC_ERROR:
	case NNP_IPC_RUNTIME_INFER_SCHEDULE_ERROR:
	case NNP_IPC_NO_SUCH_CHANNEL:
	case NNP_IPC_NO_SUCH_CMD:
		return NNP_INTERNAL_DRIVER_ERROR;
	}

	return NNP_UNKNOWN_ERROR;
}

nnpiInfContext::~nnpiInfContext()
{
	delete m_objdb;
}

int nnpiInfContext::wait_create_command(const InfContextObjID &id,
					union c2h_event_report &reply)
{
	m_waitq.wait_lock([this,id]{
			  return m_create_reply.find(id) != m_create_reply.end() ||
				 broken();
			  });

	if (broken()) {
		reply.value = m_critical_error.value;
	} else {
		reply.value = m_create_reply[id].value;
		m_create_reply.erase(id);
	}

	m_waitq.unlock();

	return 0;
}

bool nnpiInfContext::process_create_reply(const union c2h_event_report *ev)
{
	InfContextObjType t = INF_OBJ_TYPE_INVALID_OBJ_TYPE;

	switch(ev->event_code) {
	case NNP_IPC_CREATE_CONTEXT_SUCCESS:
	case NNP_IPC_CREATE_CONTEXT_FAILED:
	case NNP_IPC_RECOVER_CONTEXT_SUCCESS:
	case NNP_IPC_RECOVER_CONTEXT_FAILED:
		t = INF_OBJ_TYPE_CONTEXT;
		break;
	case NNP_IPC_CREATE_DEVRES_SUCCESS:
	case NNP_IPC_CREATE_DEVRES_FAILED:
		t = INF_OBJ_TYPE_DEVRES;
		break;
	case NNP_IPC_CREATE_COPY_SUCCESS:
	case NNP_IPC_CREATE_COPY_FAILED:
		t = INF_OBJ_TYPE_COPY;
		break;
	case NNP_IPC_CREATE_DEVNET_SUCCESS:
	case NNP_IPC_CREATE_DEVNET_FAILED:
	case NNP_IPC_DEVNET_ADD_RES_SUCCESS:
	case NNP_IPC_DEVNET_ADD_RES_FAILED:
	case NNP_IPC_DEVNET_RESOURCES_RESERVATION_SUCCESS:
	case NNP_IPC_DEVNET_RESOURCES_RELEASE_SUCCESS:
	case NNP_IPC_DEVNET_RESOURCES_RESERVATION_FAILED:
	case NNP_IPC_DEVNET_RESOURCES_RELEASE_FAILED:
	case NNP_IPC_DEVNET_SET_PROPERTY_SUCCESS:
	case NNP_IPC_DEVNET_SET_PROPERTY_FAILED:
		t = INF_OBJ_TYPE_DEVNET;
		break;
	case NNP_IPC_CREATE_INFREQ_SUCCESS:
	case NNP_IPC_CREATE_INFREQ_FAILED:
		t = INF_OBJ_TYPE_INFREQ;
		break;
	case NNP_IPC_CREATE_CMD_SUCCESS:
	case NNP_IPC_CREATE_CMD_FAILED:
		t = INF_OBJ_TYPE_CMD;
		break;
	case NNP_IPC_GET_CR_FIFO_REPLY:
	case NNP_IPC_P2P_PEERS_CONNECTED:
	case NNP_IPC_P2P_PEER_DEV_UPDATED:
		t = INF_OBJ_TYPE_P2P;
		break;

	default:
		return false;
	}

	InfContextObjID id(t,
			   ev->obj_valid ? ev->obj_id : -1,
			   ev->obj_valid_2 ? ev->obj_id_2 : -1);

	m_waitq.update_and_notify([this,id,ev,t]{
		m_create_reply[id].value = ev->value;
		if (t == INF_OBJ_TYPE_CMD && m_cmdlist_finalized_in_progress > 0)
			m_cmdlist_finalized_in_progress--;
		});

	return true;
}

int nnpiInfContext::send_create_command(const void            *buf,
					size_t                 count,
					const InfContextObjID &id,
					union c2h_event_report &reply)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	ssize_t ret;

	ret = m_chan->write(buf, count);
	if (ret != (ssize_t)count)
		return -1;

	return wait_create_command(id, reply);
}

NNPError nnpiInfContext::create(uint32_t                dev_num,
				uint8_t                 flags,
				nnpiInfContext::ptr    &out_ctx)
{
	int ret;
	NNPError err = NNP_IO_ERROR;
	nnpiInfContext::ptr ctx;
	union h2c_ChanInferenceContextOp msg;
	union c2h_event_report reply;

	ctx.reset(new nnpiInfContext(new nnpiContextObjDB()));

	//
	// Create device command channel
	//
	ret = nnpiChannel::create(dev_num,
				  3,
				  true,
				  false,
				  response_handler,
				  ctx.get(),
				  ctx->m_chan);
	if (ret != 0)
		return nnpiDevice::errnoToNNPError(ret);

	ctx->m_this = ctx;
	//
	// Create H2C and C2H ring buffers
	//
	ret = ctx->m_chan->createCommandRingBuffer(0, H2C_RINGBUF_SIZE);
	if (ret != 0)
		goto fail;

	// execute RB
	ret = ctx->m_chan->createCommandRingBuffer(1, H2C_RINGBUF_SIZE);
	if (ret != 0)
		goto fail;

	ret = ctx->m_chan->createResponseRingBuffer(0, C2H_RINGBUF_SIZE);
	if (ret != 0)
		goto fail;

	ctx->m_cmd_rb = ctx->m_chan->commandRingBuffer(0);
	ctx->m_resp_rb = ctx->m_chan->responseRingBuffer(0);

	msg.value = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_INF_CONTEXT;
	msg.chan_id = ctx->m_chan->id();
	msg.rb_id = 0;
	msg.cflags = flags;

	ret = ctx->send_create_command(&msg,
				       sizeof(msg),
				       InfContextObjID(INF_OBJ_TYPE_CONTEXT),
				       reply);
	if (ret != 0)
		goto fail;

	if (reply.event_code == NNP_IPC_CREATE_CONTEXT_FAILED) {
		err = event_valToNNPError(reply.event_val);
		goto fail;
	} else if (reply.event_code != NNP_IPC_CREATE_CONTEXT_SUCCESS) {
		goto fail;
	}

	out_ctx = ctx;

	return NNP_NO_ERROR;

fail:
	ctx->m_chan->kill();

	return err;
}

NNPError nnpiInfContext::destroy()
{
	union h2c_ChanInferenceContextOp msg;
	int ret;

	msg.value = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_INF_CONTEXT;
	msg.chan_id = m_chan->id();
	msg.rb_id = 0;
	msg.destroy = 1;

	if (!card_fatal()) {
		ret = m_chan->write(&msg, sizeof(msg));
		if (ret != sizeof(msg))
			return NNP_IO_ERROR;
	} else {
		m_chan->kill();
	}

	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::createDevRes(uint64_t    byteSize,
				      uint32_t    depth,
				      uint64_t    align,
				      uint32_t    usageFlags,
				      uint16_t   &protocol_id,
				      uint64_t   &host_addr,
				      uint8_t    &buf_id)
{
	union h2c_ChanInferenceResourceOp msg;
	union c2h_event_report reply;
	int ret;
	uint32_t id;

	ret = m_devres_ida.alloc(id);
	if (ret != 0)
		return NNP_NOT_SUPPORTED;

	protocol_id = (uint16_t)id;
	msg.value[0] = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_INF_RESOURCE;
	msg.chan_id = m_chan->id();
	msg.resID = protocol_id;
	msg.is_input = (usageFlags & NNP_RESOURCE_USAGE_NN_INPUT) ? 1 : 0;
	msg.is_output = (usageFlags & NNP_RESOURCE_USAGE_NN_OUTPUT) ? 1 : 0;
	msg.is_network = (usageFlags & NNP_RESOURCE_USAGE_NETWORK) ? 1 : 0;
	msg.is_force_4G = (usageFlags & NNP_RESOURECE_USAGE_FORCE_4G_ALLOC) ? 1 : 0;
	msg.is_ecc = (usageFlags & NNP_RESOURECE_USAGE_ECC) ? 1 : 0;
	msg.is_p2p_dst = (usageFlags & NNP_RESOURECE_USAGE_P2P_DST) ? 1 : 0;
	msg.is_p2p_src = (usageFlags & NNP_RESOURECE_USAGE_P2P_SRC) ? 1 : 0;
	msg.depth = depth;
	msg.align = align >> NNP_PAGE_SHIFT;
	msg.size = byteSize;

	ret = send_create_command(&msg,
				  sizeof(msg),
				  InfContextObjID(INF_OBJ_TYPE_DEVRES, protocol_id),
				  reply);
	if (ret != 0) {
		m_devres_ida.free(protocol_id);
		return NNP_IO_ERROR;
	}

	if (reply.event_code != NNP_IPC_CREATE_DEVRES_SUCCESS) {
		m_devres_ida.free(protocol_id);
		if (reply.event_code == NNP_IPC_CREATE_DEVRES_FAILED)
			return event_valToNNPError(reply.event_val);

		if (is_context_fatal_event(reply.event_code))
			return NNP_CONTEXT_BROKEN;
		else
			return NNP_IO_ERROR;
	}

	if ((usageFlags & NNP_RESOURECE_USAGE_P2P_DST) || (usageFlags & NNP_RESOURECE_USAGE_P2P_SRC)) {
		/* the offset is in pages */
		host_addr = m_chan->device()->bar2() + (reply.obj_id_2 << NNP_PAGE_SHIFT);
		buf_id = reply.event_val;
		nnp_log_debug(GENERAL_LOG, "New p2p dev res created (dma addr - 0x%lX buf id %u)\n", host_addr, buf_id);
	}
	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::destroyDevRes(uint16_t protocol_id)
{
	union h2c_ChanInferenceResourceOp msg;
	ssize_t n;

	msg.value[0] = 0;
	msg.value[1] = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_INF_RESOURCE;
	msg.chan_id = m_chan->id();
	msg.resID = protocol_id;
	msg.destroy = 1;

	if (card_fatal())
		return NNP_NO_ERROR;

	n = m_chan->write(&msg, sizeof(msg));
	if (n != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::markDevResDirty(uint16_t protocol_id)
{

	union h2c_ChanMarkInferenceResource msg;
	ssize_t n;

	msg.value = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_MARK_INF_RESOURCE;
	msg.chan_id = m_chan->id();
	msg.resID = protocol_id;


	if (card_fatal())
		return NNP_DEVICE_ERROR;

	n = m_chan->write(&msg, sizeof(msg));
	if (n != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;

}

NNPError nnpiInfContext::createCopy(uint16_t  devres_protocolID,
				    uint16_t  hostres_map_protocolID,
				    bool      is_c2h,
				    bool      is_subres,
				    uint16_t &out_protocolID)
{
	union h2c_ChanInferenceCopyOp msg;
	union c2h_event_report reply;
	int ret;
	uint32_t id;

	ret = m_copy_ida.alloc(id);
	if (ret != 0)
		return NNP_NOT_SUPPORTED;

	out_protocolID = (uint16_t)id;
	msg.value[0] = 0;
	msg.value[1] = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_COPY_OP;
	msg.chan_id = m_chan->id();
	msg.protResID = devres_protocolID;
	msg.protCopyID = out_protocolID;
	msg.d2d = false;
	msg.c2h = is_c2h;
	msg.destroy = 0;
	msg.subres_copy = is_subres;
	msg.hostres = hostres_map_protocolID;

	ret = send_create_command(&msg,
				  sizeof(msg),
				  InfContextObjID(INF_OBJ_TYPE_COPY, id),
				  reply);
	if (ret != 0) {
		m_copy_ida.free(out_protocolID);
		return NNP_IO_ERROR;
	}

	if (reply.event_code != NNP_IPC_CREATE_COPY_SUCCESS) {
		m_copy_ida.free(out_protocolID);
		if (reply.event_code == NNP_IPC_CREATE_COPY_FAILED)
			return event_valToNNPError(reply.event_val);

		if (is_context_fatal_event(reply.event_code))
			return NNP_CONTEXT_BROKEN;
		else
			return NNP_IO_ERROR;
	}

	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::createDeviceToDeviceCopy(uint16_t  src_devres_protocolID,
						  uint64_t  dst_devres_host_addr,
						  uint16_t  dst_devres_protocolID,
						  uint16_t  dst_devres_ctxProtocolID,
						  uint32_t  peer_devID,
						  uint16_t &out_protocolID)
{
	union h2c_ChanInferenceCopyOp msg;
	union c2h_event_report reply;
	int ret;
	uint32_t id;

	ret = m_copy_ida.alloc(id);
	if (ret != 0)
		return NNP_NOT_SUPPORTED;

	out_protocolID = (uint16_t)id;
	msg.value[0] = 0;
	msg.value[1] = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_COPY_OP;
	msg.chan_id = m_chan->id();
	msg.protResID = src_devres_protocolID;
	msg.protCopyID = out_protocolID;
	msg.d2d = true;
	msg.c2h = false;
	msg.destroy = 0;
	msg.subres_copy = false;
	msg.hostres = (dst_devres_host_addr >> NNP_PAGE_SHIFT);
	msg.peerProtResID = dst_devres_protocolID;
	msg.peerChanID = dst_devres_ctxProtocolID;
	msg.peerDevID  = (uint8_t)peer_devID;

	ret = send_create_command(&msg,
				  sizeof(msg),
				  InfContextObjID(INF_OBJ_TYPE_COPY, id),
				  reply);
	if (ret != 0) {
		m_copy_ida.free(out_protocolID);
		return NNP_IO_ERROR;
	}

	if (reply.event_code != NNP_IPC_CREATE_COPY_SUCCESS) {
		m_copy_ida.free(out_protocolID);
		if (reply.event_code == NNP_IPC_CREATE_COPY_FAILED)
			return event_valToNNPError(reply.event_val);

		if (is_context_fatal_event(reply.event_code))
			return NNP_CONTEXT_BROKEN;
		else
			return NNP_IO_ERROR;
	}


	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::destroyCopy(uint16_t protocol_id)
{
	union h2c_ChanInferenceCopyOp msg;
	ssize_t n;

	msg.value[0] = 0;
	msg.value[1] = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_COPY_OP;
	msg.chan_id = m_chan->id();
	msg.protCopyID = protocol_id;
	msg.destroy = 1;

	if (card_fatal())
		return NNP_NO_ERROR;

	n = m_chan->write(&msg, sizeof(msg));
	if (n != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::scheduleCopy(uint16_t copy_id,
				      uint64_t byteSize,
				      uint8_t  priority)
{
	ssize_t n;

	if (broken())
		return NNP_CONTEXT_BROKEN;

	if (byteSize <= 0x3fffffff && priority <= 0x3) {
		union h2c_ChanInferenceSchedCopy msg;

		msg.opcode = NNP_IPC_H2C_OP_CHAN_SCHEDULE_COPY;
		msg.chan_id = m_chan->id();
		msg.protCopyID = copy_id;
		msg.priority = priority;
		msg.copySize = byteSize;

		n = m_chan->write(&msg, sizeof(msg));
		if (n != sizeof(msg))
			return NNP_IO_ERROR;
	} else {
		union h2c_ChanInferenceSchedCopyLarge msg;

		msg.opcode = NNP_IPC_H2C_OP_CHAN_SCHEDULE_COPY_LARGE;
		msg.chan_id = m_chan->id();
		msg.protCopyID = copy_id;
		msg.priority = priority;
		msg.copySize = byteSize;

		n = m_chan->write(&msg, sizeof(msg));
		if (n != sizeof(msg))
			return NNP_IO_ERROR;
	}

	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::scheduleCopySubres(uint16_t copy_id,
					    uint16_t hostres_map_id,
					    uint64_t devres_offset,
					    uint64_t byteSize)
{
	ssize_t n;
	union h2c_ChanInferenceSchedCopySubres msg;

	if (broken())
		return NNP_CONTEXT_BROKEN;

	if ((byteSize - 1) > 0xffff)
		return NNP_INVALID_ARGUMENT;

	msg.opcode = NNP_IPC_H2C_OP_CHAN_SCHEDULE_COPY_SUBRES;
	msg.chan_id = m_chan->id();
	msg.protCopyID = copy_id;
	msg.hostres_id = hostres_map_id;
	msg.copySize = (uint16_t)(byteSize - 1);
	msg.dstOffset = devres_offset;
	n = m_chan->write(&msg, sizeof(msg));
	if (n != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::createMarker(uint32_t &out_marker)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	uint32_t marker;
	union h2c_ChanSync msg;

	marker = m_sync_point.inc();

	msg.opcode = NNP_IPC_H2C_OP_CHAN_SYNC;
	msg.chan_id = m_chan->id();
	msg.syncSeq = marker;

	if (m_chan->write(&msg, sizeof(msg)) != sizeof(msg))
		return NNP_IO_ERROR;

	out_marker = m_sync_point.getMarker();

	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::waitMarker(uint32_t marker,
				    uint32_t timeout_us)
{
	SyncPoint sp(marker);
	NNPError ret = NNP_NO_ERROR;

	auto cond = [this,sp] {
			bool ret = m_last_completed_sync_point >= sp ||
				   m_failed_sync_points.find(sp.val()) != m_failed_sync_points.end() ||
				   (broken() && !aborted());
			return ret;
			};

	bool found;

	if (timeout_us == UINT32_MAX) {
		m_waitq.wait_lock(cond);
		found = true;
	} else {
		found = m_waitq.wait_timeout_lock(timeout_us, cond);
	}

	if (!found)
		ret = NNP_TIMED_OUT;
	else if (broken())
		ret = NNP_CONTEXT_BROKEN;
	else if (m_failed_sync_points.find(sp.val()) != m_failed_sync_points.end()) {
		m_failed_sync_points.erase(sp.val());
		ret = NNP_BROKEN_MARKER;
	}

	m_waitq.unlock();
	return ret;
}

void nnpiInfContext::parseErrorEvent(union c2h_event_report *ev,
				     NNPCriticalErrorInfo  *out_err)
{
	out_err->errorMessageSize = 0;

	switch(ev->event_code) {
	case NNP_IPC_ERROR_OS_CRASHED:
		out_err->nnpCriticalError = NNP_CRI_INTERNAL_DRIVER_ERROR;
		out_err->objType = NNP_FAIL_OBJ_TYPE_CARD;
		break;
	case NNP_IPC_ERROR_RUNTIME_DIED:
		out_err->nnpCriticalError = NNP_CRI_INTERNAL_DRIVER_ERROR;
		out_err->objType = NNP_FAIL_OBJ_TYPE_CONTEXT;
		break;
	case NNP_IPC_ERROR_RUNTIME_LAUNCH:
		out_err->nnpCriticalError = NNP_CRI_INTERNAL_DRIVER_ERROR;
		out_err->objType = NNP_FAIL_OBJ_TYPE_CONTEXT;
		break;
	case NNP_IPC_ERROR_CARD_RESET:
		out_err->nnpCriticalError = NNP_CRI_CARD_RESET;
		out_err->objType = NNP_FAIL_OBJ_TYPE_CARD;
		break;
	case NNP_IPC_EXECUTE_COPY_SUBRES_FAILED: //fall through
	case NNP_IPC_EXECUTE_COPY_FAILED:
		{
			nnpiCopyCommand::ptr copy = m_objdb->getCopy(ev->obj_id);

			out_err->nnpCriticalError = NNP_CRI_INTERNAL_DRIVER_ERROR;
			out_err->objType = NNP_FAIL_OBJ_TYPE_COPY;
			if (copy.get())
				out_err->obj.copy.copyHandle = copy->user_hdl();
			else
				out_err->obj.copy.copyHandle = 0;
		}
		break;
	case NNP_IPC_SCHEDULE_INFREQ_FAILED:
		{
			nnpiInfReq::ptr infreq = m_objdb->getInfReq(ev->obj_id,
								    ev->obj_id_2);
			if (infreq.get()) {
				out_err->obj.infreq.devnetHandle = infreq->network()->user_hdl();;
				out_err->obj.infreq.infreqHandle = infreq->user_hdl();
			} else {
				out_err->obj.infreq.devnetHandle = 0;
				out_err->obj.infreq.infreqHandle = 0;
			}
			out_err->nnpCriticalError = NNP_CRI_INTERNAL_DRIVER_ERROR;
			out_err->objType = NNP_FAIL_OBJ_TYPE_INFREQ;
		}
		break;
	case NNP_IPC_ABORT_REQUEST:
		out_err->nnpCriticalError = NNP_CRI_GRACEFUL_DESTROY;
		out_err->objType = NNP_FAIL_OBJ_TYPE_NONE;
		break;
	default:
		out_err->nnpCriticalError = NNP_CRI_UNKNOWN_CRITICAL_ERROR;
		out_err->objType = NNP_FAIL_OBJ_TYPE_NONE;
	}
}

void nnpiInfContext::parseExecError(nnpiExecErrorList *list,
				    uint32_t idx,
				    NNPCriticalErrorInfo *out_err)
{
	const struct ipc_exec_error_desc *desc = list->getDesc(idx);
	if (!desc)
		return;

	if (desc->cmd_type == CMDLIST_CMD_INFREQ) {
		out_err->objType = NNP_FAIL_OBJ_TYPE_INFREQ;

		nnpiInfReq::ptr infreq = m_objdb->getInfReq(desc->devnet_id, desc->obj_id);
		if (infreq.get()) {
			out_err->obj.infreq.devnetHandle = infreq->network()->user_hdl();;
			out_err->obj.infreq.infreqHandle = infreq->user_hdl();
		} else {
			out_err->obj.infreq.devnetHandle = 0;
			out_err->obj.infreq.infreqHandle = 0;
		}
	} else if (desc->cmd_type == CMDLIST_CMD_COPY) {
		out_err->objType = NNP_FAIL_OBJ_TYPE_COPY;
		out_err->nnpCriticalError = NNP_CRI_INTERNAL_DRIVER_ERROR;
		nnpiCopyCommand::ptr copy = m_objdb->getCopy(desc->obj_id);
		if (copy.get())
			out_err->obj.copy.copyHandle = copy->user_hdl();
		else
			out_err->obj.copy.copyHandle = 0;
	} else if (desc->cmd_type == CMDLIST_CMD_COPYLIST) {
		out_err->objType = NNP_FAIL_OBJ_TYPE_COPY;
		out_err->nnpCriticalError = NNP_CRI_INTERNAL_DRIVER_ERROR;
		out_err->obj.copy.copyHandle = 0;
	} else {
		out_err->objType = NNP_FAIL_OBJ_TYPE_CONTEXT;
		out_err->nnpCriticalError = NNP_CRI_INTERNAL_DRIVER_ERROR;
	}

	out_err->errorMessageSize = desc->error_msg_size;

	switch (desc->event_val) {
	case NNP_IPC_FAILED_TO_RELEASE_CREDIT:
		out_err->nnpCriticalError = NNP_CRI_FAILED_TO_RELEASE_CREDIT;
		break;
	case NNP_IPC_INPUT_IS_DIRTY:
		out_err->nnpCriticalError = NNP_CRI_INPUT_IS_DIRTY;
		break;
	case NNP_IPC_ICEDRV_INFER_EXEC_ERROR:
		out_err->nnpCriticalError = NNP_CRI_INFREQ_FAILED;
		break;
	case NNP_IPC_ICEDRV_INFER_EXEC_ERROR_NEED_RESET:
		out_err->nnpCriticalError = NNP_CRI_INFREQ_NETWORK_RESET;
		break;
	case NNP_IPC_ICEDRV_INFER_EXEC_ERROR_NEED_CARD_RESET:
		out_err->nnpCriticalError = NNP_CRI_INFREQ_CARD_RESET;
		break;
	case NNP_IPC_NOT_SUPPORTED:
		out_err->nnpCriticalError = NNP_CRI_NOT_SUPPORTED;
		break;
	case NNP_IPC_IO_ERROR:
	default:
		out_err->nnpCriticalError = NNP_CRI_INTERNAL_DRIVER_ERROR;
	}
}

NNPError nnpiInfContext::sendQueryErrorList(uint32_t cmd_id, bool for_clear)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	union h2c_ExecErrorList msg;
	ssize_t ret;

	msg.value = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_EXEC_ERROR_LIST;
	msg.chan_id = m_chan->id();
	if (cmd_id <= USHRT_MAX) {
		msg.cmdID = cmd_id;
		msg.cmdID_valid = 1;
	}
	msg.clear = (for_clear ? 1 : 0);

	ret = m_chan->write(&msg, sizeof(msg));
	if (ret != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::waitErrorListQueryCompletion(nnpiExecErrorList *list)
{
	m_waitq.wait([this,list] { return list->queryCompleted() || broken(); });
	if (broken())
		return NNP_CONTEXT_BROKEN;

	return event_valToNNPError(list->completionEventVal());
}

NNPError nnpiInfContext::waitCriticalError(NNPCriticalErrorInfo *out_err,
					   uint32_t              timeout_us)
{
	NNPError ret = NNP_NO_ERROR;

	if (!out_err)
		return NNP_INVALID_ARGUMENT;

	auto cond = [this] {
		bool ret = m_critical_error.value != 0;
		return ret;
	};

	bool found;
	union c2h_event_report critical_error;

	if (timeout_us == UINT32_MAX) {
		m_waitq.wait_lock(cond);
		found = true;
	} else {
		found = m_waitq.wait_timeout_lock(timeout_us, cond);
	}

	critical_error.value = m_critical_error.value;
	if (critical_error.event_code == NNP_IPC_CONTEXT_EXEC_ERROR)
		m_critical_error.value = 0;

	m_waitq.unlock();

	if (!found) {
		out_err->nnpCriticalError = NNP_CRI_NO_ERROR;
		if (timeout_us > 0)
			return NNP_TIMED_OUT;
		else
			return NNP_NO_ERROR;
	}

	if (critical_error.event_code != NNP_IPC_CONTEXT_EXEC_ERROR) {
		parseErrorEvent(&critical_error, out_err);
	} else {
		m_errorList.clear();
		m_errorList.startQuery();
		NNPError ret = sendQueryErrorList(UINT_MAX);
		if (ret != NNP_NO_ERROR)
			return ret;

		ret = waitErrorListQueryCompletion(&m_errorList);
		if (ret == NNP_NO_ERROR)
			parseExecError(&m_errorList, 0, out_err);

		m_waitq.lock();
		if (m_critical_error.value == 0)
			m_critical_error.value = critical_error.value;
		m_waitq.unlock();
	}

	return ret;
}

NNPError nnpiInfContext::recover()
{
	NNPError ret = NNP_NO_ERROR;
	union c2h_event_report critical_save;

	m_waitq.lock();
	if (!broken()) {
		m_waitq.unlock();
		return ret;
	} else if (card_fatal()) {
		m_waitq.unlock();
		return NNP_DEVICE_ERROR;
	} else if (m_critical_error.event_code == NNP_IPC_ABORT_REQUEST) {
		m_waitq.unlock();
		return NNP_CONTEXT_BROKEN;
	}
	critical_save.value = m_critical_error.value;
	m_critical_error.value = 0;
	m_waitq.unlock();

	m_errorList.startQuery();
	ret = sendQueryErrorList(UINT_MAX, true);
	if (ret == NNP_NO_ERROR)
		ret = waitErrorListQueryCompletion(&m_errorList);

	m_waitq.lock();
	if (ret != NNP_NO_ERROR && m_critical_error.value == 0)
		m_critical_error.value = critical_save.value;
	m_waitq.unlock();

	return ret;
}

void nnpiInfContext::processExecErrorList(union c2h_ExecErrorList *msg)
{
	nnpiCommandList::ptr cmdlist;
	nnpiExecErrorList *list = &m_errorList;

	if (!msg->is_error &&  msg->clear_status == 0)
		m_resp_rb->updateTailBy(NNP_PAGE_SIZE);

	if (msg->cmdID_valid) {
		cmdlist = m_objdb->getCommandList(msg->cmdID);
		if (!cmdlist.get()) {
			uint16_t cmdlistID = msg->cmdID;
			nnp_log_err(GENERAL_LOG, "Got error list for not existing cmdlist %u\n", cmdlistID);
			return;
		}
		list = cmdlist->getErrorList();
	}

	if (msg->is_error) {
		list->appendErrorListPacket(m_waitq, NULL, 0, 0, msg->total_size);
		return;
	}

	if (msg->clear_status == 0) {
		uint32_t bsize, cont;
		void *packet = m_resp_rb->lockAvailSpace(NNP_PAGE_SIZE,
							 bsize,
							 cont,
							 0);
		if (!packet) {
			list->appendErrorListPacket(m_waitq, NULL, 0, 0, NNP_IPC_IO_ERROR);
		} else {
			list->appendErrorListPacket(m_waitq, packet, msg->pkt_size + 1, msg->total_size);
			m_resp_rb->unlockAvailSpace(NNP_PAGE_SIZE);
			if(!m_chan->sendResponseRingBufferHeadUpdate(0, NNP_PAGE_SIZE))
				nnp_log_err(GENERAL_LOG, "FATAL: failed to update response ring bufer head!!!\n");
		}
	} else {
		list->clearRequestSucceeded(m_waitq);
	}
}

void nnpiInfContext::failAllScheduledCopyCommands()
{
	m_objdb->for_each_copy([this](nnpiCopyCommand::ptr copy) {
				copy->postSchedule(&m_errorList);
			      });
}

void nnpiInfContext::completeAllCommandLists()
{
	m_objdb->for_each_cmdlist([](nnpiCommandList::ptr cmd) {
					cmd->complete();
				});
}

bool nnpiInfContext::response_handler(const void     *_ctx,
				      const uint64_t *response,
				      uint32_t        response_size)
{
	nnpiInfContext *ctx = (nnpiInfContext *)_ctx;
	union c2h_chan_msg_header *msg = (union c2h_chan_msg_header *)response;

	if (response == nullptr) {
		ctx->failAllScheduledCopyCommands();
		/* channel was killed - destroy context */
		/* make it broken to wakeup all */
		auto set_killed = [ctx] {
						ctx->m_critical_error.opcode = NNP_IPC_C2H_OP_EVENT_REPORT;
						ctx->m_critical_error.event_code = NNP_IPC_ERROR_CHANNEL_KILLED;
					};
		if (!ctx->card_fatal() && response_size == 0) {
			ctx->m_waitq.update_and_notify(set_killed);
			ctx->completeAllCommandLists();
		} else { // killed from atfork, don't take lock, no need to notify
			set_killed();
		}
		ctx->m_objdb->clearAll();

		nnpi_utils_reset_m_this(ctx->m_this);
		return true;
	}

	if (msg->opcode == NNP_IPC_C2H_OP_EVENT_REPORT) {
		union c2h_event_report *ev = (union c2h_event_report *)msg;

		if (is_card_fatal_drv_event(ev->event_code)) {
			ctx->failAllScheduledCopyCommands();
			ctx->m_waitq.update_and_notify([ctx,ev]{
							if (ctx->m_critical_error.value == 0)
								ctx->m_critical_error.value = ev->value;
							});
			ctx->completeAllCommandLists();
			ctx->m_objdb->clearAll();
			nnpi_utils_reset_m_this(ctx->m_this);
			return true;
		} else if (is_card_fatal_event(ev->event_code) ||
			   is_context_fatal_event(ev->event_code) ||
			   ev->event_code == NNP_IPC_ABORT_REQUEST) {

			if (is_card_fatal_event(ev->event_code)) {
				ctx->failAllScheduledCopyCommands();
				ctx->m_chan->set_kill_on_exit();
			}
			ctx->m_waitq.update_and_notify([ctx,ev]{
							if (ctx->m_critical_error.value == 0 ||
							    ev->event_code == NNP_IPC_ABORT_REQUEST)
								ctx->m_critical_error.value = ev->value;
							});
			ctx->completeAllCommandLists();

			return false;
		}

		assert(ev->context_id == ctx->m_chan->id());

		if (ev->event_code == NNP_IPC_EXECUTE_COPY_SUCCESS ||
		    ev->event_code == NNP_IPC_EXECUTE_COPY_FAILED  ||
		    ev->event_code == NNP_IPC_EXECUTE_COPY_SUBRES_SUCCESS  ||
		    ev->event_code == NNP_IPC_EXECUTE_COPY_SUBRES_FAILED) {
			nnpiCopyCommand::ptr copy = ctx->m_objdb->getCopy(ev->obj_id);
			if (!copy.get())
				nnp_log_err(GENERAL_LOG, "Got execute copy event for not existing copy %d\n", ev->obj_id);
			else {
				nnpiCommandList::ptr cmdlist;

				if (ev->obj_valid_2) {
					cmdlist = ctx->m_objdb->getCommandList(ev->obj_id_2);
					if (!cmdlist.get()) {
						nnp_log_err(GENERAL_LOG, "Got copy complete for not existing cmdlist %d\n", ev->obj_id_2);
						return false;
					}
				}

				//
				// If copy failed - record failure in the host
				// resource and error list of the
				// context/command list
				//
				if (ev->event_code == NNP_IPC_EXECUTE_COPY_FAILED ||
				    ev->event_code == NNP_IPC_EXECUTE_COPY_SUBRES_FAILED) {
					nnpiHostRes::ptr hostres = copy->hostres();

					if (hostres.get() != nullptr) {
						if (ev->obj_valid_2)
							copy->postSchedule(cmdlist->getErrorList());
						else
							copy->postSchedule(&ctx->m_errorList);
					}
				} else {
					copy->postSchedule();
				}

				if (ev->obj_valid_2) {
					if (ev->event_code == NNP_IPC_EXECUTE_COPY_SUCCESS ||
					    ev->event_code == NNP_IPC_EXECUTE_COPY_SUBRES_SUCCESS)
						cmdlist->complete();
					else
						cmdlist->addError(ev);
				}
			}
		} else if (ev->event_code == NNP_IPC_EXECUTE_CPYLST_SUCCESS ||
			   ev->event_code == NNP_IPC_EXECUTE_CPYLST_FAILED) {
			nnpiCommandList::ptr cmdlist = ctx->m_objdb->getCommandList(ev->obj_id);
			if (!cmdlist.get()) {
				nnp_log_err(GENERAL_LOG, "Got cpylst complete for not existing cmdlist %d\n", ev->obj_id);
			} else {
				nnpiInfCommandSchedParams *cpylst = cmdlist->getCommand(ev->obj_id_2);
				if (ev->event_code == NNP_IPC_EXECUTE_CPYLST_SUCCESS) {
					cpylst->schedule_done();
					if (ev->event_val == NNP_IPC_CMDLIST_FINISHED)
						cmdlist->complete();
					else if (ev->event_val != 0)
						nnp_log_err(GENERAL_LOG, "Got cpylst complete with not supported event_val %u\n", ev->event_val);
				} else {
					cpylst->schedule_done(cmdlist->getErrorList());
					cmdlist->addError(ev);
				}
			}
		} else if (ev->event_code == NNP_IPC_EXECUTE_CMD_COMPLETE) {
			nnpiCommandList::ptr cmdlist = ctx->m_objdb->getCommandList(ev->obj_id);
			if (!cmdlist.get())
				nnp_log_err(GENERAL_LOG, "Got cmdlist complete for not existing cmdlist %d\n", ev->obj_id);
			else {
				if (ev->event_val != 0)
					cmdlist->addError(ev);
				cmdlist->complete();
			}
		} else if (ev->event_code == NNP_IPC_DEVRES_DESTROYED) {
			ctx->freeDevResID(ev->obj_id);
		} else if (ev->event_code == NNP_IPC_DEVNET_DESTROYED) {
			nnpiDevNet::ptr devnet = ctx->m_objdb->getDevNet(ev->obj_id);
			if (!devnet.get())
				nnp_log_err(GENERAL_LOG, "Got network destroy for not existing network %d\n", ev->obj_id);
			else
				ctx->m_objdb->removeDevNet(ev->obj_id);
			ctx->freeDevNetID(ev->obj_id);
		} else if (ev->event_code == NNP_IPC_INFREQ_DESTROYED) {
			nnpiDevNet::ptr devnet = ctx->m_objdb->getDevNet(ev->obj_id_2);
			if (!devnet.get())
				nnp_log_err(GENERAL_LOG, "Got infreq destroy for not existing network %d\n", ev->obj_id);
			else {
				ctx->m_objdb->removeInfReq(ev->obj_id_2, ev->obj_id);
				devnet->freeInfReqID(ev->obj_id);
			}
		} else if (ev->event_code == NNP_IPC_COPY_DESTROYED) {
			nnpiCopyCommand::ptr copy = ctx->m_objdb->getCopy(ev->obj_id);
			if (!copy.get())
				nnp_log_err(GENERAL_LOG, "Got copy destroy for not existing copy %d\n", ev->obj_id);
			else
				ctx->m_objdb->removeCopy(ev->obj_id);
			ctx->freeCopyID(ev->obj_id);
		} else if (ev->event_code == NNP_IPC_CMD_DESTROYED) {
			nnpiCommandList::ptr cmdlist = ctx->m_objdb->getCommandList(ev->obj_id);
			if (!cmdlist.get())
				nnp_log_err(GENERAL_LOG, "Got cmdlist destroy for not existing cmdlist %d\n", ev->obj_id);
			else
				ctx->m_objdb->removeCommandList(ev->obj_id);
			ctx->m_cmdlist_ida.free(ev->obj_id);
		} else if (ev->event_code == NNP_IPC_CONTEXT_DESTROYED) {
			// flag channel response read thread to exit
			nnpi_utils_reset_m_this(ctx->m_this);
			return true;
		} else if (ev->event_code == NNP_IPC_CREATE_SYNC_FAILED) {
			ctx->m_waitq.update_and_notify([ctx,ev]{ ctx->m_failed_sync_points.insert(ev->obj_id); });
		} else if (ev->event_code == NNP_IPC_EC_FAILED_TO_RELEASE_CREDIT) {
			nnpiCommandList::ptr cmdlist;

			if (ev->obj_valid) {
				cmdlist = ctx->m_objdb->getCommandList(ev->obj_id);
				if (!cmdlist.get()) {
					nnp_log_err(GENERAL_LOG, "Got release credit failed for not existing cmdlist %d\n", ev->obj_id);
					return false;
				} else
					cmdlist->addError(ev);
			}
		} else {
			ctx->process_create_reply(ev);
		}

		return false;
	} else if (msg->opcode == NNP_IPC_C2H_OP_CHAN_SYNC_DONE) {
		union c2h_ChanSyncDone *sync = (union c2h_ChanSyncDone *)msg;
		ctx->m_waitq.update_and_notify([ctx,sync]{ ctx->m_last_completed_sync_point.set(sync->syncSeq); });
	} else if (msg->opcode == NNP_IPC_C2H_OP_CHAN_INFREQ_FAILED) {
		union c2h_ChanInfReqFailed *reqfail = (union c2h_ChanInfReqFailed *)msg;
		union c2h_event_report event;

		event.value = 0;
		event.opcode = NNP_IPC_C2H_OP_EVENT_REPORT;
		event.event_code = NNP_IPC_SCHEDULE_INFREQ_FAILED;
		event.event_val = reqfail->reason;
		event.context_id = reqfail->chan_id;
		event.obj_id = reqfail->infreqID;
		event.obj_id_2 = reqfail->netID;
		event.ctx_valid = 1;
		event.obj_valid = 1;
		event.obj_valid_2 = 1;

		if (reqfail->cmdID_valid) {
			nnpiCommandList::ptr cmdlist = ctx->m_objdb->getCommandList(reqfail->cmdID);
			if (!cmdlist.get()) {
				uint16_t cmdlist_id = reqfail->cmdID;

				nnp_log_err(GENERAL_LOG, "Got infreq complete not existing cmdlist %u\n", cmdlist_id);
			}
			else
				cmdlist->addError(&event);
		}
	} else if (msg->opcode == NNP_IPC_C2H_OP_CHAN_EXEC_ERROR_LIST) {
		ctx->processExecErrorList((union c2h_ExecErrorList *)msg);
	} else {
		nnp_log_err(IPC_LOG, "Unexpected opcode received %d on channel %d\n", (int)msg->opcode, ctx->m_chan->id());
	}

	return false;
}

NNPError nnpiInfContext::trace_user_data(const char *key,
					 uint64_t    user_data)
{
	union h2c_ChanTraceUserData msg;
	uint64_t keyU64 = 0;
	size_t size = (strlen(key) < USER_DATA_MAX_KEY_SIZE) ? strlen(key) : USER_DATA_MAX_KEY_SIZE;

	memset(&msg, 0, sizeof(msg));
	msg.opcode = NNP_IPC_H2C_OP_CHAN_TRACE_USER_DATA;
	msg.chan_id = m_chan->id();
	msg.user_data = user_data;
	memcpy_s(&keyU64, sizeof(uint64_t), key, size);
	msg.key = keyU64;

	if (m_chan->write(&msg, sizeof(msg)) != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}

NNPError nnpiInfContext::send_user_handle(InfContextObjType type,
					 uint16_t id1,
					 uint16_t id2,
					 uint64_t user_handle)
{
	union h2c_ChanIdsMap msg;

	memset(&msg, 0, sizeof(msg));
	msg.opcode = NNP_IPC_H2C_OP_CHAN_IDS_MAP;
	msg.chan_id = m_chan->id();
	msg.objType = type;
	msg.val1 = id1;
	msg.val2 = id2;
	msg.user_handle = user_handle;

	if (m_chan->write(&msg, sizeof(msg)) != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}
