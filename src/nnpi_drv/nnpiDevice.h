/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include <memory>
#include <vector>
#include <stdint.h>
#include <unordered_set>
#include <mutex>
#include <map>
#include "nnpiHostProc.h"
#include "nnpdrvTypes.h"

template <class T>
class safeVector : public std::vector<T> {
public:

	safeVector()
	{
	}

	virtual ~safeVector()
	{
		this->clear();
	}

	inline void lock()
	{
		sv_mutex.lock();
	}

	inline void unlock()
	{
		sv_mutex.unlock();
	}

private:
	std::mutex sv_mutex;
};

class nnpiDevice {
public:
	typedef std::shared_ptr<nnpiDevice> ptr;
	typedef safeVector<std::weak_ptr<nnpiDevice>> vec;

	static int findMaxDeviceNumber(void);
	static nnpiDevice::ptr get(uint32_t dev_num);
	static NNPError errnoToNNPError(uint8_t nnp_kernel_error);

	static void close_devices();
	static void clear_devices(bool only_contexts);

	void close_all_chan_fds(bool only_contexts);

	~nnpiDevice()
	{
		if (m_fd >= 0)
			close(m_fd);
		if (!m_chan_fds.empty())
			close_all_chan_fds(false);
	}

	inline uint32_t number() const { return m_dev_num; }

	inline uint64_t bar0() const {return bar0_addr;}
	inline uint64_t bar2() const {return bar2_addr;}

	int createChannel(const nnpiHostProc::ptr &host,
			  uint32_t                 weight,
			  bool                     is_context,
			  bool                     get_device_events,
			  uint16_t                *out_id,
			  int                     *out_fd,
			  int                     *out_privileged);

	void closeChannel(int fd);

	static void lock_all();
	static void unlock_all();

	int createChannelRingBuffer(uint16_t  channel_id,
				    uint8_t   rb_id,
				    bool      is_h2c,
				    nnpiHostRes::ptr hostres);

	int destroyChannelRingBuffer(uint16_t  channel_id,
				     uint8_t   rb_id,
				     bool      is_h2c);

	int mapHostResource(uint16_t         channel_id,
			    nnpiHostRes::ptr hostres,
			    uint16_t        &out_map_id);

	int unmapHostResource(uint16_t  channel_id,
			      uint16_t  map_id);
private:
	int getBARAddr();

	nnpiDevice(uint32_t dev_num,
		   int fd) :
		m_dev_num(dev_num),
		m_fd(fd),
		bar0_addr(0),
		bar2_addr(0)
	{
	}

private:
	static nnpiDevice::vec s_devices;
	std::mutex m_mutex;
	std::map<int, bool> m_chan_fds;
	const uint32_t m_dev_num;
	int m_fd;
	uint64_t bar0_addr;
	uint64_t bar2_addr;

};
