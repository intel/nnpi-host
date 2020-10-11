/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiInfReq.h"
#include <string.h>
#include <stdint.h>
#include "ipc_c2h_events.h"
#include "ipc_chan_protocol.h"
#include "nnpiContextObjDB.h"
#include "safe_lib.h"

NNPError nnpiInfReq::create(nnpiDevNet::ptr         devnet,
			    const nnpiDevRes::vec  &inputs,
			    const nnpiDevRes::vec  &outputs,
			    const void             *config_data,
			    uint32_t                config_data_size,
			    nnpiInfReq::ptr        &outInfReq)
{
	union h2c_ChanInferenceReqOp msg;
	uint32_t packet_size;
	uint16_t protocol_id;
	union c2h_event_report reply;
	NNPError ret;
	int rc;

	if (!config_data && config_data_size > 0)
		return NNP_INVALID_ARGUMENT;

	if (!devnet->valid())
		return NNP_INCOMPLETE_NETWORK;

	if (devnet->context()->broken())
		return NNP_CONTEXT_BROKEN;

	packet_size = 3 * sizeof(uint32_t) +
		      (inputs.size() + outputs.size()) * sizeof(uint16_t) +
		      config_data_size;

	if (packet_size >= NNP_PAGE_SIZE ||
	    outputs.size() == 0)
		return NNP_NOT_SUPPORTED;

	for (nnpiDevRes::vec::const_iterator i = inputs.begin();
	     i != inputs.end();
	     i++)
		if (!((*i)->usageFlags() & NNP_RESOURCE_USAGE_NN_INPUT))
			return NNP_INCOMPATIBLE_RESOURCES;

	for (nnpiDevRes::vec::const_iterator i = outputs.begin();
	     i != outputs.end();
	     i++)
		if (!((*i)->usageFlags() & NNP_RESOURCE_USAGE_NN_OUTPUT))
			return NNP_INCOMPATIBLE_RESOURCES;

	ret = devnet->allocInfReqID(protocol_id);
	if (ret != NNP_NO_ERROR)
		return ret;

	nnpiInfContext::ptr ctx(devnet->context());
	std::lock_guard<std::mutex> lock(ctx->mutex());
	nnpiRingBuffer::ptr cmd_ring(ctx->chan()->commandRingBuffer(0));

	msg.value = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_INF_REQ_OP;
	msg.chan_id = ctx->chan()->id();
	msg.netID = devnet->id();
	msg.infreqID = protocol_id;
	msg.rb_id = 0;
	msg.destroy = 0;
	msg.size = packet_size;

	uint32_t cont;
	uint32_t *ptr = (uint32_t *)cmd_ring->lockFreeSpace(NNP_PAGE_SIZE, cont);
	if (!ptr)
		return NNP_IO_ERROR;

	*(ptr++) = (uint32_t)inputs.size();
	*(ptr++) = (uint32_t)outputs.size();
	*(ptr++) = config_data_size;

	uint16_t *ptr16 = (uint16_t *)ptr;

	for (nnpiDevRes::vec::const_iterator i = inputs.begin();
	     i != inputs.end();
	     i++)
		*(ptr16++) = (*i)->id();

	for (nnpiDevRes::vec::const_iterator i = outputs.begin();
	     i != outputs.end();
	     i++)
		*(ptr16++) = (*i)->id();

	if (config_data_size > 0)
		memcpy_s(ptr16, NNP_PAGE_SIZE-((size_t)ptr16-(size_t)ptr)-3*sizeof(uint32_t), config_data, config_data_size);

	if (ctx->chan()->write(&msg, sizeof(msg)) != sizeof(msg)) {
		cmd_ring->unlockFreeSpace(0);
		return NNP_IO_ERROR;
	}

	cmd_ring->unlockFreeSpace(NNP_PAGE_SIZE);

	rc = ctx->wait_create_command(InfContextObjID(INF_OBJ_TYPE_INFREQ, protocol_id, devnet->id()),
				      reply);
	if (rc != 0)
		return NNP_IO_ERROR;

	if (reply.event_code == NNP_IPC_CREATE_INFREQ_FAILED)
		return event_valToNNPError(reply.event_val);
	else if (is_context_fatal_event(reply.event_code))
		return NNP_CONTEXT_BROKEN;
	else if (reply.event_code != NNP_IPC_CREATE_INFREQ_SUCCESS)
		return NNP_IO_ERROR;

	outInfReq.reset(new nnpiInfReq(devnet,
				       protocol_id,
				       inputs,
				       outputs));

	ctx->objdb()->insertInfReq(protocol_id, outInfReq);

	return ret;
}

NNPError nnpiInfReq::destroy()
{
	union h2c_ChanInferenceReqOp msg;
	nnpiInfContext::ptr ctx(m_devnet->context());

	msg.value = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_INF_REQ_OP;
	msg.chan_id = ctx->chan()->id();
	msg.netID = m_devnet->id();
	msg.infreqID = m_id;
	msg.destroy = 1;

	if (ctx->card_fatal())
		return NNP_NO_ERROR;

	if (ctx->chan()->write(&msg, sizeof(msg)) != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}

NNPError nnpiInfReq::schedule(nnpdrvinfSchedParams *schedParams)
{
	h2c_ChanInferenceReqSchedule msg;
	nnpiInfContext::ptr ctx(m_devnet->context());

	if (!ctx->wait_can_schedule())
		return NNP_CONTEXT_BROKEN;

	msg.value[0] = 0;
	msg.value[1] = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_SCHEDULE_INF_REQ;
	msg.chan_id = ctx->chan()->id();
	msg.netID = m_devnet->id();
	msg.infreqID = m_id;
	if (schedParams != NULL) {
		msg.batchSize = schedParams->batchSize;
		msg.priority = schedParams->priority;
		msg.debugOn = schedParams->debugOn;
		msg.collectInfo = schedParams->collectInfo;
		msg.schedParamsIsNull = 0;
	} else {
		msg.batchSize = 0;
		msg.priority = 0;
		msg.debugOn = 0;
		msg.collectInfo = 0;
		msg.schedParamsIsNull = 1;
	}

	if (ctx->chan()->write(&msg, sizeof(msg)) != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}
