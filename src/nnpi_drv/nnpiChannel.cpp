/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiChannel.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "nnpiHostProc.h"
#include "nnpdrvInference.h"
#include "ipc_chan_protocol.h"
#include "nnp_log.h"

static nnpiActiveContexts::ptr s_active_contexts;

nnpiActiveContexts::nnpiActiveContexts()
{
}

nnpiActiveContexts::~nnpiActiveContexts()
{
}

void nnpiActiveContexts::lock()
{
	// call this with nnpiGlobalLock locked
	if (s_active_contexts.get()) {
		s_active_contexts->m_waitq.lock();
	}
}

void nnpiActiveContexts::unlock()
{
	// call this with nnpiGlobalLock locked
	if (s_active_contexts.get()) {
		s_active_contexts->m_waitq.unlock();
	}
}

nnpiActiveContexts::ptr nnpiActiveContexts::add(nnpiChannel *chan)
{
	nnpiActiveContexts::ptr ret;

	nnpiGlobalLock();
	if (!s_active_contexts.get())
		s_active_contexts.reset(new nnpiActiveContexts());
	if (s_active_contexts.get()) {
		nnpiActiveContexts *active = s_active_contexts.get();
		active->m_waitq.update_and_notify([active,chan]{ active->m_context_chans.insert(chan); });
		ret = s_active_contexts;
	}
	nnpiGlobalUnlock();

	return ret;
}

void nnpiActiveContexts::remove(nnpiChannel *chan)
{
	m_waitq.update_and_notify([this,chan]{ m_context_chans.erase(chan); });
	nnpiGlobalLock();
	if (m_context_chans.size() == 0)
		s_active_contexts.reset();
	nnpiGlobalUnlock();
}

void nnpiActiveContexts::kill_all(bool force, bool umd_only)
{
	bool killed;

	/* kill all active contexts */
	do {
		killed = false;
		m_waitq.lock();
		for (std::set<nnpiChannel *>::iterator c = m_context_chans.begin();
		     c != m_context_chans.end();
		     c++) {
			if (!(*c)->killed() && (force || (*c)->should_be_killed_on_exit())) {
				m_waitq.unlock();
				(*c)->kill(umd_only);
				killed = true;
				break;
			}
		}
	} while(killed);

	m_waitq.unlock();
}

void nnpiActiveContexts::close_all()
{
	nnpiActiveContexts::ptr active = s_active_contexts;

	if (active.get() == nullptr)
		return;

	active.get()->kill_all(true, true);
}

void nnpiActiveContexts::destroy()
{
	nnpiActiveContexts::ptr active = s_active_contexts;

	if (active.get() == nullptr)
		return;

	active->m_waitq.update_and_notify([active]{ active->m_context_chans.clear(); });
	nnpiGlobalLock();
	s_active_contexts.reset();
	nnpiGlobalUnlock();
}

void nnpiActiveContexts::wait_all()
{
	nnpiActiveContexts::ptr active = s_active_contexts;

	/* wait for destruction */
	if (active.get() != nullptr) {
		active.get()->kill_all(false, false);
		active.get()->m_waitq.wait([active] { return active->m_context_chans.size() == 0; });
	}
}

int nnpiChannel::create(uint32_t                dev_num,
			uint32_t                weight,
			bool                    is_context,
			bool                    get_device_events,
			nnpiChannel::handler_cb response_handler,
			void                   *response_handler_ctx,
			nnpiChannel::ptr       &out_channel_ptr)
{
	nnpiHostProc::ptr host(nnpiHostProc::get());
	nnpiDevice::ptr dev(nnpiDevice::get(dev_num));
	uint16_t id;
	int fd;
	int privileged;
	int ret;

	if (host.get() == nullptr || dev.get() == nullptr)
		return ENODEV;

	ret = dev->createChannel(host, weight, is_context, get_device_events, &id, &fd, &privileged);
	if (ret != 0)
		return ret;

	out_channel_ptr.reset(new nnpiChannel(host,
					      dev,
					      id,
					      is_context,
					      fd,
					      privileged != 0,
					      get_device_events,
					      response_handler,
					      response_handler_ctx));

	if (is_context)
		out_channel_ptr->m_active_ref = nnpiActiveContexts::add(out_channel_ptr.get());

	out_channel_ptr->m_this = out_channel_ptr;
	if (pthread_create(&out_channel_ptr->m_resp_thread, NULL, nnpiChannel::response_handler, out_channel_ptr.get()) != 0)
		goto fail;
	out_channel_ptr->m_joined = false;
	return 0;

fail:
	nnpi_utils_reset_m_this(out_channel_ptr->m_this);
	out_channel_ptr.reset();
	return ENOMEM;
}

nnpiChannel::~nnpiChannel()
{
	/*
	 * When we get here the response thread is either
	 * canceled and joined, or is currently exiting.
	 * if it did not joined, detach it.
	 */
	if (!m_joined)
		pthread_detach(m_resp_thread);

	for (unsigned int i = 0; i < MAX_CHANNEL_RINGBUFS; ++i) {
		destroyCommandRingBuffer(i);
		destroyResponseRingBuffer(i);
	}

	m_dev->closeChannel(m_fd);

	if (m_is_context && m_active_ref.get()) {
		m_active_ref->remove(this);
	}
}

void nnpiChannel::kill(bool umd_only)
{
	if (m_killed)
		return;

	if (!umd_only) {
		/* cancel and wait for thread to terminate */
		pthread_cancel(m_resp_thread);
		pthread_join(m_resp_thread, NULL);
		m_joined = true;

		/*
		 * Nothing else to do if thread was canceled after it
		 * already handled its exit
		 */
		if (!m_this.get())
			return;
	}

	handleResponseHandlerExit(true, umd_only);
}

void nnpiChannel::handleResponseHandlerExit(bool abnormal, bool umd_only)
{
	if (!umd_only)
		for(uint8_t i=0; i<MAX_CHANNEL_RINGBUFS;i++) {
			nnpiRingBuffer::ptr cmdRingBuffer =
				commandRingBuffer(i);
			if(cmdRingBuffer.get() != NULL)
				cmdRingBuffer->setInvalid();
		}

	// if abnormally existed - inform handler by sending NULL response
	if (abnormal) {
		m_killed = true;
		(*m_resp_handler)(m_resp_handler_ctx, NULL, (umd_only ? 1 : 0));
	}

	nnpi_utils_reset_m_this(m_this);
}

void *nnpiChannel::response_handler(void *ctx)
{
	uint64_t msg[16];
	ssize_t n;
	bool should_exit = false;
	nnpiChannel *channel = (nnpiChannel *)ctx;

	do {
		n = read(channel->m_fd, msg, sizeof(msg));
		if (n > 0) {
			union c2h_chan_msg_header *cmd = (union c2h_chan_msg_header *)msg;
			if (cmd->opcode == NNP_IPC_C2H_OP_CHANNEL_RB_UPDATE) {
				channel->handle_ringbuff_head_update((union c2h_ChanRingBufUpdate *)cmd);
				continue;
			}

			should_exit = (*channel->m_resp_handler)(channel->m_resp_handler_ctx,
								 msg,
								 n << 3);
		} else if (n == 0 || errno != EINTR || channel->m_killed)
			break;
	} while(!should_exit);

	channel->handleResponseHandlerExit(!should_exit, false);

	return NULL;
}

int nnpiChannel::createCommandRingBuffer(uint8_t  id,
					 uint32_t byte_size)
{
	nnpiHostRes::ptr hostRes;
	int ret;

	if (id >= MAX_CHANNEL_RINGBUFS)
		return EINVAL;

	if (m_cmd_ringbufs[id].get() != nullptr)
		return EBUSY;

	if (m_killed)
		return EBUSY;

	ret = nnpiHostRes::create(byte_size,
				  NNP_RESOURCE_USAGE_NN_INPUT,
				  hostRes);
	if (ret != 0)
		return ret;

	ret = m_dev->createChannelRingBuffer(m_id,
					     id,
					     true,
					     hostRes);
	if (ret != 0)
		return ret;

	m_cmd_ringbufs[id].reset(new nnpiRingBuffer(hostRes));

	return 0;
}

int nnpiChannel::createResponseRingBuffer(uint8_t  id,
					  uint32_t byte_size)
{
	nnpiHostRes::ptr hostRes;
	int ret;

	if (id >= MAX_CHANNEL_RINGBUFS)
		return EINVAL;

	if (m_resp_ringbufs[id].get() != nullptr)
		return EBUSY;

	if (m_killed)
		return EBUSY;

	ret = nnpiHostRes::create(byte_size,
				  NNP_RESOURCE_USAGE_NN_OUTPUT,
				  hostRes);
	if (ret != 0)
		return ret;

	ret = m_dev->createChannelRingBuffer(m_id,
					     id,
					     false,
					     hostRes);
	if (ret != 0)
		return ret;

	m_resp_ringbufs[id].reset(new nnpiRingBuffer(hostRes));

	return 0;
}

int nnpiChannel::destroyCommandRingBuffer(uint8_t id)
{
	if (id >= MAX_CHANNEL_RINGBUFS)
		return EINVAL;

	if (m_cmd_ringbufs[id].get() == nullptr)
		return ENXIO;

	m_cmd_ringbufs[id].reset();

	return m_dev->destroyChannelRingBuffer(m_id, id, true);
}

int nnpiChannel::destroyResponseRingBuffer(uint8_t id)
{
	if (id >= MAX_CHANNEL_RINGBUFS)
		return EINVAL;

	if (m_resp_ringbufs[id].get() == nullptr)
		return ENXIO;

	m_resp_ringbufs[id].reset();

	return m_dev->destroyChannelRingBuffer(m_id, id, false);
}

bool nnpiChannel::sendResponseRingBufferHeadUpdate(uint8_t rb_id, uint32_t size)
{
	union h2c_ChanRingBufUpdate cmd = {.reserved=0};

	if (m_killed)
		return false;

	if (rb_id < MAX_CHANNEL_RINGBUFS &&
	    m_resp_ringbufs[rb_id].get() != nullptr) {
		cmd.opcode = NNP_IPC_H2C_OP_CHANNEL_RB_UPDATE;
		cmd.chan_id = m_id;
		cmd.rb_id = rb_id;
		cmd.size = size;

		if(::write(m_fd, &cmd, sizeof(cmd)) < (ssize_t)sizeof(cmd))
			return false;
	}

	return true;
}

void nnpiChannel::handle_ringbuff_head_update(union c2h_ChanRingBufUpdate *cmd)
{
	if (m_killed)
		return;

	if (m_cmd_ringbufs[cmd->rb_id].get() == nullptr)
		nnp_log_err(GENERAL_LOG, "Got ringbuf update for not existence ringbuf %u\n", (uint32_t)cmd->rb_id);
	else
		m_cmd_ringbufs[cmd->rb_id]->updateHead(cmd->size);
}
