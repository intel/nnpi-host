/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include <memory>
#include <stdint.h>
#include <map>
#include "nnpiHostProc.h"
#include "nnpiWaitQueue.h"
#include <mutex>

class nnpiRingBuffer {
public:
	typedef std::shared_ptr<nnpiRingBuffer> ptr;

	explicit nnpiRingBuffer(nnpiHostRes::ptr hostres) :
		m_hostres(hostres),
		m_buf((uint8_t *)hostres->vaddr()),
		m_size((uint32_t)hostres->size()),
		m_head(0),
		m_tail(0),
		m_is_full(false),
		m_invalid(false)
	{
	}

	inline uint32_t head() const { return m_head; }

	inline uint32_t getFreeBytes()
	{
		if (m_is_full)
			return 0;
		else if (m_tail >= m_head)
			return (m_head + m_size - m_tail);
		else
			return (m_head - m_tail);
	}

	inline uint32_t getAvailBytes()
	{
		if (m_is_full)
			return m_size;
		else if (m_head > m_tail)
			return (m_tail + m_size - m_head);
		else
			return (m_tail - m_head);
	}

	void push(void *buf, uint32_t size);
	void pop(void *buf, uint32_t size);

	void *lockFreeSpace(uint32_t size,
			    uint32_t &outContSize,
			    uint32_t timeout_us = UINT32_MAX)
	{
		bool avail;

		auto cond = [this, size]{
			bool ret = getFreeBytes() >= size || m_invalid;
			return ret;
		};

		if (timeout_us == UINT32_MAX) {
			m_waitq.wait_lock(cond);
			avail = true;
		} else {
			avail = m_waitq.wait_timeout_lock(timeout_us, cond);
		}

		if (!m_invalid && avail) {
			uint32_t end_dist = m_size - m_tail;

			if (end_dist >= size)
				outContSize = size;
			else
				outContSize = end_dist;

			return m_buf + m_tail;
		}

		outContSize = 0;
		m_waitq.unlock();
		return NULL;
	}

	void unlockFreeSpace(uint32_t size)
	{
		if (size > 0) {
			m_tail = (m_tail + size) % m_size;
			if (m_tail == m_head)
				m_is_full = true;
		}
		m_waitq.unlock_notify();
	}

	void updateTailBy(uint32_t size)
	{
		m_waitq.lock();
		m_tail = (m_tail + size) % m_size;
		if (m_tail == m_head)
			m_is_full = true;
		m_waitq.unlock();
	}

	void *lockAvailSpace(uint32_t minAvail,
			     uint32_t &outSize,
			     uint32_t &outContSize,
			     uint32_t timeout_us = UINT32_MAX)
	{
		uint32_t avail = 0;
		uint32_t *pavail = &avail;

		auto cond = [this, pavail, minAvail] {
			bool ret = (*pavail = getAvailBytes()) >= minAvail;
			return ret;
		};

		if (timeout_us == UINT32_MAX)
			m_waitq.wait_lock(cond);
		else
			m_waitq.wait_timeout_lock(timeout_us, cond);

		outSize = avail;

		uint32_t end_dist = m_size - m_head;

		if (end_dist >= avail)
			outContSize = avail;
		else
			outContSize = end_dist;

		if (avail >= minAvail) {
			return m_buf + m_head;
		}

		m_waitq.unlock();
		return NULL;
	}

	void unlockAvailSpace(uint32_t size)
	{
		if (size > 0) {
			m_head = (m_head + size) % m_size;
			m_is_full = false;
			m_waitq.unlock_notify();
		}
	}

	void updateHead(uint32_t size)
	{
		if (size > 0) {
			m_waitq.lock();
			m_head = (m_head + size) % m_size;
			m_is_full = false;
			m_waitq.unlock_notify();
		}
	}

	void setInvalid()
	{
		m_waitq.update_and_notify([this] { m_invalid = true; });
	}

	const uint8_t *buf() const { return m_buf; }

private:
	nnpiHostRes::ptr m_hostres;
	nnpiWaitQueue    m_waitq;
	uint8_t * const  m_buf;
	const uint32_t   m_size;
	uint32_t         m_head;
	uint32_t         m_tail;
	bool             m_is_full;
	bool             m_invalid;
};

class nnpiIDA {
public:
	explicit nnpiIDA(uint32_t max_id)
	{
		m_free_ranges[0] = max_id;
		m_num_alloc = 0;
	}

	int alloc(uint32_t &out_id);
	void free(uint32_t id);
	uint32_t get_num_alloc();

private:
	std::map<uint32_t, uint32_t> m_free_ranges;
	std::mutex m_mutex;
	uint32_t m_num_alloc;
};

template<class T>
inline void nnpi_utils_reset_m_this(std::shared_ptr<T> &m_this)
{
	std::shared_ptr<T> local_m_this(m_this);
	m_this.reset();
};
