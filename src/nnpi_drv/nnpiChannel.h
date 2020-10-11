/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include <memory>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include "nnpiDevice.h"
#include "nnpiHostProc.h"
#include "nnpiUtils.h"
#include <set>

#define MAX_CHANNEL_RINGBUFS 2

union c2h_ChanRingBufUpdate;

class nnpiChannel;

class nnpiActiveContexts {
public:
	typedef std::shared_ptr<nnpiActiveContexts> ptr;

	nnpiActiveContexts();
	~nnpiActiveContexts();

	static void lock();
	static void unlock();

	static nnpiActiveContexts::ptr add(nnpiChannel *chan);
	void remove(nnpiChannel *chan);
	void kill_all(bool force, bool umd_only);
	static void close_all();
	static void destroy();
	static void wait_all();

private:
	bool                    m_destroyed;
	std::set<nnpiChannel *> m_context_chans;
	nnpiWaitQueue           m_waitq;
};

class nnpiChannel {
public:
	typedef std::shared_ptr<nnpiChannel> ptr;
	typedef bool (*handler_cb)(const void     *ctx,
				   const uint64_t *response,
				   uint32_t        response_size);

	static int create(uint32_t                dev_num,
			  uint32_t                weight,
			  bool                    is_context,
			  bool                    get_device_events,
			  nnpiChannel::handler_cb response_handler,
			  void                   *response_handler_ctx,
			  nnpiChannel::ptr       &out_channel_ptr);

	void kill(bool umd_only = false);
	~nnpiChannel();

	int createCommandRingBuffer(uint8_t  id,
				    uint32_t  byte_size);

	int destroyCommandRingBuffer(uint8_t id);

	int createResponseRingBuffer(uint8_t  id,
				     uint32_t byte_size);

	int destroyResponseRingBuffer(uint8_t id);

	nnpiDevice::ptr device() { return m_dev; }
	uint16_t id() const { return m_id; }
	bool privileged() const { return m_privileged; }

	inline ssize_t write(const void *buf, size_t count)
	{
		if (m_killed)
			return 0;
		return ::write(m_fd, buf, count);
	}

	inline void set_kill_on_exit() { m_kill_on_exit = true; }

	inline bool should_be_killed_on_exit() { return m_kill_on_exit; }

	nnpiRingBuffer::ptr responseRingBuffer(uint8_t id)
	{
		return m_resp_ringbufs[id];
	}

	nnpiRingBuffer::ptr commandRingBuffer(uint8_t id)
	{
		return m_cmd_ringbufs[id];
	}

	bool sendResponseRingBufferHeadUpdate(uint8_t rb_id, uint32_t size);

	bool killed() const { return m_killed; }
private:
	static void *response_handler(void *ctx);

	void handle_ringbuff_head_update(union c2h_ChanRingBufUpdate *cmd);
	void handleResponseHandlerExit(bool abnormal, bool umd_only);

	nnpiChannel(const nnpiHostProc::ptr &proc,
		    const nnpiDevice::ptr   &dev,
		    uint16_t                 id,
		    bool                     is_context,
		    int                      fd,
		    bool                     privileged,
		    bool                     get_device_events,
		    nnpiChannel::handler_cb  handler,
		    void                    *handler_ctx) :
		m_proc(proc),
		m_dev(dev),
		m_id(id),
		m_is_context(is_context),
		m_fd(fd),
		m_privileged(privileged),
		m_listen_device_events(get_device_events),
		m_resp_handler(handler),
		m_resp_handler_ctx(handler_ctx),
		m_joined(true),
		m_killed(false),
		m_kill_on_exit(false)
	{
	}

private:
	nnpiChannel::ptr m_this; /* holds refcount to himself,
				  * released by response thread when it exits
				  */
	nnpiHostProc::ptr m_proc;
	nnpiDevice::ptr m_dev;
	const uint16_t m_id;
	const bool     m_is_context;
	const int m_fd;
	const bool m_privileged;
	const bool m_listen_device_events;
	const nnpiChannel::handler_cb m_resp_handler;
	const void  *m_resp_handler_ctx;
	bool m_joined;
	bool m_killed;
	bool m_kill_on_exit;
	nnpiActiveContexts::ptr m_active_ref;
	nnpiRingBuffer::ptr m_cmd_ringbufs[MAX_CHANNEL_RINGBUFS];
	nnpiRingBuffer::ptr m_resp_ringbufs[MAX_CHANNEL_RINGBUFS];
	pthread_t m_resp_thread;
};
