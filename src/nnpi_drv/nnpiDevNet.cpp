/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiDevNet.h"
#include <string.h>
#include <stdint.h>
#include "ipc_c2h_events.h"
#include "ipc_chan_protocol.h"
#include "nnpiContextObjDB.h"
#include "safe_lib.h"

static NNPError send_create_or_add(nnpiInfContext::ptr     ctx,
				   uint32_t                protocol_id,
				   bool                    is_create,
				   const nnpiDevRes::vec  &devres_vec,
				   const void             *config_data,
				   uint32_t                config_data_size)
{
	union h2c_ChanInferenceNetworkOp msg;
	uint64_t total_data_size;
	union c2h_event_report reply;
	NNPError ret = NNP_NO_ERROR;
	int rc;

	std::lock_guard<std::mutex> lock(ctx->mutex());
	nnpiRingBuffer::ptr cmd_ring(ctx->chan()->commandRingBuffer(0));

	if (devres_vec.size() > 0x1000000)
		return NNP_NOT_SUPPORTED;

	total_data_size = devres_vec.size() * sizeof(uint16_t) + config_data_size;
	if (total_data_size > 0x100000000)
		return NNP_NOT_SUPPORTED;

	if (ctx->broken())
		return NNP_CONTEXT_BROKEN;

	memset(&msg, 0, sizeof(msg));
	msg.opcode = NNP_IPC_H2C_OP_CHAN_INF_NETWORK;
	msg.chan_id = ctx->chan()->id();
	msg.netID = protocol_id;
	msg.rb_id = 0;
	msg.destroy = 0;
	msg.create = is_create;
	msg.num_res = devres_vec.size();
	msg.size = total_data_size - 1;
	msg.start_res_idx = 0;
	msg.chained = (total_data_size > NNP_PAGE_SIZE ? 1 : 0);

	uint32_t sent = 0;
	uint32_t sent_conf = 0;
	uint32_t max_devres_per_page = (NNP_PAGE_SIZE / sizeof(uint16_t));

	do {
		uint32_t cont;
		uint8_t *ptr = (uint8_t *)cmd_ring->lockFreeSpace(NNP_PAGE_SIZE, cont);
		if (!ptr) {
			ret = NNP_IO_ERROR;
			break;
		}

		uint32_t n_res = (devres_vec.size() - sent);
		if (n_res > max_devres_per_page)
			n_res = max_devres_per_page;

		uint16_t *ptr16 = (uint16_t *)ptr;
		for (uint32_t i = 0; i < n_res; i++)
			ptr16[i] = devres_vec[sent + i]->id();

		sent += n_res;
		ptr += (n_res * sizeof(uint16_t));

		uint32_t space_left = NNP_PAGE_SIZE - (n_res * sizeof(uint16_t));
		if (space_left > 0 && sent_conf < config_data_size) {
			uint32_t n_conf = config_data_size - sent_conf;
			if (n_conf > space_left)
				n_conf = space_left;

			memcpy_s(ptr, space_left, ((uint8_t *)config_data) + sent_conf, n_conf);
			sent_conf += n_conf;
		}

		if (ctx->chan()->write(&msg, sizeof(msg)) != sizeof(msg)) {
			cmd_ring->unlockFreeSpace(0);
			ret = NNP_IO_ERROR;
			break;
		}

		cmd_ring->unlockFreeSpace(NNP_PAGE_SIZE);

		msg.start_res_idx += n_res;

	} while(sent < devres_vec.size() || sent_conf < config_data_size);

	if (ret == NNP_NO_ERROR) {
		rc = ctx->wait_create_command(InfContextObjID(INF_OBJ_TYPE_DEVNET, protocol_id),
					      reply);
		if (rc != 0)
			return NNP_IO_ERROR;

		if (reply.event_code == NNP_IPC_CREATE_DEVNET_FAILED ||
		    reply.event_code == NNP_IPC_DEVNET_ADD_RES_FAILED)
			return event_valToNNPError(reply.event_val);
		else if (is_context_fatal_event(reply.event_code))
			return NNP_CONTEXT_BROKEN;
		else if (reply.event_code != NNP_IPC_CREATE_DEVNET_SUCCESS &&
			 reply.event_code != NNP_IPC_DEVNET_ADD_RES_SUCCESS)
			return NNP_IO_ERROR;
	}

	return ret;
}

NNPError nnpiDevNet::create(nnpiInfContext::ptr     ctx,
			    const nnpiDevRes::vec  &devres_vec,
			    const void             *config_data,
			    uint32_t                config_data_size,
			    nnpiDevNet::ptr        &outDevnet)
{
	NNPError ret;
	uint16_t protocol_id;

	ret = ctx->allocDevNetID(protocol_id);
	if (ret != NNP_NO_ERROR)
		return ret;

	ret = send_create_or_add(ctx,
				 protocol_id,
				 true,
				 devres_vec,
				 config_data,
				 config_data_size);
	if (ret != NNP_NO_ERROR)
		return ret;

	outDevnet.reset(new nnpiDevNet(ctx,
				       protocol_id,
				       devres_vec));

	ctx->objdb()->insertDevNet(protocol_id, outDevnet);

	return NNP_NO_ERROR;
}

NNPError nnpiDevNet::add_resources(const nnpiDevRes::vec  &devres_vec,
				   const void             *config_data,
				   uint32_t                config_data_size)
{
	NNPError ret;

	if (m_infreq_ida.get_num_alloc() > 0)
		return NNP_NOT_SUPPORTED;

	ret = send_create_or_add(m_ctx,
				 m_id,
				 false,
				 devres_vec,
				 config_data,
				 config_data_size);

	if (ret == NNP_NO_ERROR)
		m_devres_vec.insert(m_devres_vec.end(), devres_vec.begin(), devres_vec.end());

	return ret;
}

NNPError nnpiDevNet::setProperty(NNPNetPropertiesType property,
			uint32_t property_val,
			uint32_t timeout_us)
{
	union h2c_ChanInferenceNetworkSetProperty msg;
	union c2h_event_report reply;
	int rc;

	msg.opcode = NNP_IPC_H2C_OP_CHAN_NETWORK_PROPERTY;
	msg.chan_id = m_ctx->chan()->id();
	msg.netID = m_id;
	msg.timeout = timeout_us;
	msg.property = property;
	msg.property_val = property_val;

	if (m_ctx->chan()->write(&msg, sizeof(msg)) != sizeof(msg))
		return NNP_IO_ERROR;

	rc = m_ctx->wait_create_command(InfContextObjID(INF_OBJ_TYPE_DEVNET, m_id),
					reply);
	if (rc != 0)
		return NNP_IO_ERROR;

	if (reply.event_code == NNP_IPC_DEVNET_RESOURCES_RESERVATION_FAILED ||
	    reply.event_code == NNP_IPC_DEVNET_RESOURCES_RELEASE_FAILED ||
	    reply.event_code == NNP_IPC_DEVNET_SET_PROPERTY_FAILED)
		return event_valToNNPError(reply.event_val);
	else if (is_context_fatal_event(reply.event_code))
		return NNP_CONTEXT_BROKEN;
	else if (reply.event_code != NNP_IPC_DEVNET_RESOURCES_RESERVATION_SUCCESS &&
		 reply.event_code != NNP_IPC_DEVNET_RESOURCES_RELEASE_SUCCESS &&
		 reply.event_code != NNP_IPC_DEVNET_SET_PROPERTY_SUCCESS)
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}

NNPError nnpiDevNet::destroy()
{
	union h2c_ChanInferenceNetworkOp msg;
	NNPError ret = NNP_NO_ERROR;

	memset(&msg, 0, sizeof(msg));
	msg.opcode = NNP_IPC_H2C_OP_CHAN_INF_NETWORK;
	msg.chan_id = m_ctx->chan()->id();
	msg.netID = m_id;
	msg.destroy = 1;

	if (m_ctx->card_fatal())
		return NNP_NO_ERROR;

	if (m_ctx->chan()->write(&msg, sizeof(msg)) != sizeof(msg))
		ret = NNP_IO_ERROR;

	return ret;
}
