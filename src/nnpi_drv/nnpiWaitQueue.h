/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include <stdint.h>
#include <condition_variable>
#include <chrono>
#include <mutex>

class nnpiWaitQueue {
public:
	nnpiWaitQueue()
	{
	}

	std::mutex &mutex() { return m_mutex; }

	template <class UpdateOp>
	inline void update_and_notify(UpdateOp update_op)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		update_op();
		m_cv.notify_all();
	}

	template <class Pred>
	inline void wait(Pred cond)
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		m_cv.wait(lock, cond);
	}

	template <class Pred>
	inline bool wait_timeout(uint32_t usec, Pred cond)
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		return m_cv.wait_for(lock, std::chrono::microseconds(usec), cond);
	}

	template <class Pred>
	inline void wait_lock(Pred cond)
	{
		std::unique_lock<std::mutex> lock(m_mutex);

		m_cv.wait(lock, cond);

		lock.release();
	}

	template <class Pred>
	inline bool wait_timeout_lock(uint32_t usec, Pred cond)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		bool ret;

		ret = m_cv.wait_for(lock, std::chrono::microseconds(usec), cond);

		if (ret)
			lock.release();

		return ret;
	}

	inline void lock()
	{
		m_mutex.lock();
	}

	inline void unlock()
	{
		m_mutex.unlock();
	}

	inline void unlock_notify()
	{
		m_mutex.unlock();
		m_cv.notify_all();
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_cv;
};
