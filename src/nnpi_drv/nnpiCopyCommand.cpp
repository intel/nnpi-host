/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiCopyCommand.h"
#include "nnpiContextObjDB.h"
#include "nnp_log.h"
#include "ipc_chan_protocol.h"

/* Offset of most significant door bell byte */
#define MSB_DB_OFFSET 0x37

NNPError nnpiCopyCommand::create(nnpiInfContext::ptr ctx,
				 nnpiDevRes::ptr     devres,
				 nnpiHostRes::ptr    hostres,
				 bool                is_c2h,
				 nnpiCopyCommand::ptr  &outCopy)
{
	nnpiDevice::ptr dev(ctx->device());
	uint16_t hostres_map_id;
	uint16_t protocol_id;
	int rc;
	NNPError ret;

	if (devres->size() != hostres->size()) {
		nnp_log_err(CREATE_COMMAND_LOG, "Device and host resource must be the same size\n");
		return NNP_INCOMPATIBLE_RESOURCES;
	}

	if ((devres->usageFlags() & NNP_RESOURCE_USAGE_NETWORK) != 0) {
		nnp_log_err(CREATE_COMMAND_LOG, "Cannot create copy for network resource\n");
		return NNP_INCOMPATIBLE_RESOURCES;
	}

	if ((is_c2h && !(devres->usageFlags() & NNP_RESOURCE_USAGE_NN_OUTPUT)) ||
	    (!is_c2h && !(devres->usageFlags() & NNP_RESOURCE_USAGE_NN_INPUT))) {
		nnp_log_err(CREATE_COMMAND_LOG, "device resource usage not matching copy direction\n");
		return NNP_INCOMPATIBLE_RESOURCES;
	}

	if ((is_c2h && !(hostres->usageFlags() & NNP_RESOURCE_USAGE_NN_OUTPUT)) ||
	    (!is_c2h && !(hostres->usageFlags() & NNP_RESOURCE_USAGE_NN_INPUT))) {
		nnp_log_err(CREATE_COMMAND_LOG, "host resource usage not matching copy direction\n");
		return NNP_INCOMPATIBLE_RESOURCES;
	}

	rc = dev->mapHostResource(ctx->chan()->id(),
				  hostres,
				  hostres_map_id);
	if (rc != 0)
		return nnpiDevice::errnoToNNPError(rc);

	ret = ctx->createCopy(devres->id(),
			      hostres_map_id,
			      is_c2h,
			      false,
			      protocol_id);

	if (ret == NNP_NO_ERROR) {
		outCopy.reset(new nnpiCopyCommand(ctx,
						  protocol_id,
						  devres,
						  hostres,
						  hostres_map_id,
						  is_c2h));
		ctx->objdb()->insertCopy(protocol_id, outCopy);
		ctx->send_user_handle(INF_OBJ_TYPE_COPY, hostres_map_id, COPY_USER_HANDLE_TYPE_HOSTRES, hostres->get_user_hdl());
	} else {
		dev->unmapHostResource(ctx->chan()->id(),
				       hostres_map_id);
	}

	return ret;
}

NNPError nnpiCopyCommand::update_peers(nnpiDevRes::ptr dst_devres, nnpiDevRes::ptr src_devres)
{
	union h2c_ChanGetCrFIFO get_cr_fifo_msg;
	union h2c_ChanUpdatePeerDev update_peer_dev_msg;
	union c2h_event_report reply;
	int rc;
	uint64_t rel_cr_fifo_addr;
	uint64_t fw_cr_fifo_addr;
	NNPError ret;

	/* Ask producer device for release credit FIFO */
	get_cr_fifo_msg.chan_id = src_devres->m_ctx->chan()->id();
	get_cr_fifo_msg.opcode = NNP_IPC_H2C_OP_CHAN_P2P_GET_CR_FIFO;
	get_cr_fifo_msg.p2p_tr_id = src_devres->m_ctx->getP2PtransactionID();
	get_cr_fifo_msg.fw_fifo = 0;
	get_cr_fifo_msg.peer_id = dst_devres->m_ctx->device()->number();

	rc = src_devres->m_ctx->send_create_command(&get_cr_fifo_msg,
				  sizeof(get_cr_fifo_msg),
				  InfContextObjID(INF_OBJ_TYPE_P2P, get_cr_fifo_msg.p2p_tr_id),
				  reply);
	if (rc != 0)
		return NNP_IO_ERROR;
	else if (src_devres->m_ctx->broken())
		return NNP_CONTEXT_BROKEN;
	else if (reply.event_val != 0)
		return event_valToNNPError(reply.event_val);

	rel_cr_fifo_addr = src_devres->m_ctx->chan()->device()->bar2() + (reply.obj_id_2 << NNP_PAGE_SHIFT);

	/* Ask consumer device for forward credit FIFO */
	get_cr_fifo_msg.chan_id = dst_devres->m_ctx->chan()->id();
	get_cr_fifo_msg.opcode = NNP_IPC_H2C_OP_CHAN_P2P_GET_CR_FIFO;
	get_cr_fifo_msg.p2p_tr_id = dst_devres->m_ctx->getP2PtransactionID();
	get_cr_fifo_msg.fw_fifo = 1;
	get_cr_fifo_msg.peer_id = src_devres->m_ctx->device()->number();

	rc = dst_devres->m_ctx->send_create_command(&get_cr_fifo_msg,
				  sizeof(get_cr_fifo_msg),
				  InfContextObjID(INF_OBJ_TYPE_P2P, get_cr_fifo_msg.p2p_tr_id),
				  reply);
	if (rc != 0)
		return NNP_IO_ERROR;
	else if (dst_devres->m_ctx->broken())
		return NNP_CONTEXT_BROKEN;
	else if (reply.event_val != 0)
		return event_valToNNPError(reply.event_val);

	fw_cr_fifo_addr = dst_devres->m_ctx->chan()->device()->bar2() + (reply.obj_id_2 << NNP_PAGE_SHIFT);

	/* Send to producer device cr fifo addr and db addr of consumer */
	update_peer_dev_msg.chan_id = src_devres->m_ctx->chan()->id();
	update_peer_dev_msg.opcode = NNP_IPC_H2C_OP_CHAN_P2P_UPDATE_PEER_DEV;
	update_peer_dev_msg.p2p_tr_id = src_devres->m_ctx->getP2PtransactionID();
	update_peer_dev_msg.dev_id = dst_devres->m_ctx->device()->number();
	update_peer_dev_msg.is_producer = 0;
	update_peer_dev_msg.cr_fifo_addr = (fw_cr_fifo_addr >> NNP_PAGE_SHIFT);
	update_peer_dev_msg.db_addr = dst_devres->m_ctx->chan()->device()->bar0() + MSB_DB_OFFSET;

	rc = src_devres->m_ctx->send_create_command(&update_peer_dev_msg,
				  sizeof(update_peer_dev_msg),
				  InfContextObjID(INF_OBJ_TYPE_P2P, update_peer_dev_msg.p2p_tr_id),
				  reply);
	if (rc != 0)
		return NNP_IO_ERROR;
	else if (src_devres->m_ctx->broken())
		return NNP_CONTEXT_BROKEN;
	else if (reply.event_val != 0)
		return event_valToNNPError(reply.event_val);

	/* Send to consumer device cr fifo addr and db addr of producer */
	update_peer_dev_msg.chan_id = dst_devres->m_ctx->chan()->id();
	update_peer_dev_msg.opcode = NNP_IPC_H2C_OP_CHAN_P2P_UPDATE_PEER_DEV;
	update_peer_dev_msg.p2p_tr_id = dst_devres->m_ctx->getP2PtransactionID();
	update_peer_dev_msg.dev_id = src_devres->m_ctx->device()->number();
	update_peer_dev_msg.is_producer = 1;
	update_peer_dev_msg.cr_fifo_addr = (rel_cr_fifo_addr >> NNP_PAGE_SHIFT);
	update_peer_dev_msg.db_addr = src_devres->m_ctx->chan()->device()->bar0() + MSB_DB_OFFSET;

	rc = dst_devres->m_ctx->send_create_command(&update_peer_dev_msg,
				  sizeof(update_peer_dev_msg),
				  InfContextObjID(INF_OBJ_TYPE_P2P, update_peer_dev_msg.p2p_tr_id),
				  reply);
	if (rc != 0)
		return NNP_IO_ERROR;
	else if (dst_devres->m_ctx->broken())
		return NNP_CONTEXT_BROKEN;
	else if (reply.event_val != 0)
		return event_valToNNPError(reply.event_val);

	/* Connect peers on producer device */
	ret = src_devres->d2d_pair(dst_devres);
	if (ret != NNP_NO_ERROR)
		return ret;

	/* Connect peers on consumer device */
	ret = dst_devres->d2d_pair(src_devres);
	if (ret != NNP_NO_ERROR) {
		src_devres->d2d_pair(nullptr);
		return ret;
	}

	return NNP_NO_ERROR;
}

void nnpiCopyCommand::unpair_d2d_devreses()
{
	if (!m_is_d2d)
		return;

	/* Unpair peer on producer device */
	m_src_devres->d2d_pair(nullptr);

	/* Unpair peer on consumer device */
	m_devres->d2d_pair(nullptr);
}

NNPError nnpiCopyCommand::create_d2d(nnpiInfContext::ptr ctx,
				 nnpiDevRes::ptr     dst_devres,
				 nnpiDevRes::ptr     src_devres,
				 nnpiCopyCommand::ptr  &outCopy)
{
	nnpiDevice::ptr dev(ctx->device());
	uint16_t protocol_id;
	NNPError ret;


	if (src_devres->size() != dst_devres->size()) {
		nnp_log_err(CREATE_COMMAND_LOG, "Resources must be the same size\n");
		return NNP_INCOMPATIBLE_RESOURCES;
	}

	if (!(dst_devres->usageFlags() & NNP_RESOURECE_USAGE_P2P_DST)) {
		nnp_log_err(CREATE_COMMAND_LOG, "Wrong destination resource\n");
		return NNP_INCOMPATIBLE_RESOURCES;
	}

	if (!(src_devres->usageFlags() & NNP_RESOURECE_USAGE_P2P_SRC)) {
		nnp_log_err(CREATE_COMMAND_LOG, "Wrong source resource\n");
		return NNP_INCOMPATIBLE_RESOURCES;
	}

	if (src_devres->m_ctx->device()->number() != ctx->device()->number()) {
		nnp_log_err(CREATE_COMMAND_LOG, "Copy object should be allocated on producer device\n");
		return NNP_INCOMPATIBLE_RESOURCES;
	}

	if (src_devres->m_ctx->device()->number() == dst_devres->m_ctx->device()->number()) {
		nnp_log_err(CREATE_COMMAND_LOG, "Device resources should be allocated on different devices\n");
		return NNP_INCOMPATIBLE_RESOURCES;
	}

	ret = update_peers(dst_devres, src_devres);
	if (ret != NNP_NO_ERROR)
		nnp_log_err(CREATE_COMMAND_LOG, "Couldn't update peers\n");

	ret = ctx->createDeviceToDeviceCopy(src_devres->id(),
					    dst_devres->hostAddr(),
					    dst_devres->id(),
					    dst_devres->m_ctx->chan()->id(),
					    dst_devres->m_ctx->device()->number(),
					    protocol_id);

	if (ret == NNP_NO_ERROR) {
		outCopy.reset(new nnpiCopyCommand(ctx,
						  protocol_id,
						  dst_devres,
						  src_devres));
		ctx->objdb()->insertCopy(protocol_id, outCopy);
	}

	return ret;
}

NNPError nnpiCopyCommand::create_subres(nnpiDevRes::ptr     devres,
					nnpiCopyCommand::ptr  &outCopy)
{
	nnpiInfContext::ptr ctx(devres->m_ctx);
	uint16_t protocol_id;
	NNPError ret;

	ret = ctx->createCopy(devres->id(),
			      0,
			      false,
			      true,
			      protocol_id);

	if (ret == NNP_NO_ERROR) {
		outCopy.reset(new nnpiCopyCommand(ctx,
						  protocol_id,
						  devres));
		ctx->objdb()->insertCopy(protocol_id, outCopy);
	}

	return ret;
}

nnpiCopyCommand::~nnpiCopyCommand()
{
	if (!m_is_subres && !m_is_d2d)
		m_ctx->device()->unmapHostResource(m_ctx->chan()->id(),
						   m_hostres_map_id);
}

NNPError nnpiCopyCommand::destroy()
{
	return m_ctx->destroyCopy(m_id);
}
