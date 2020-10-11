/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include <stdint.h>
#include <map>
#include <memory>
#include <algorithm>
#include <mutex>

template <class T, class Key>
class nnpiHandleMap {
public:
	typedef std::map<Key, std::shared_ptr<T>> map;
	typedef void (*cb_func)(T *iter);

	inline Key makeHandle(std::shared_ptr<T> obj)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		Key hdl = (Key)((uintptr_t)obj.get());
		if (obj.get()) {
			while(m_map.find(hdl) != m_map.end())
				hdl++;
			m_map[hdl] = obj;
		}
		return hdl;
	}

	inline std::shared_ptr<T> find(Key hdl)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::shared_ptr<T> ret;
		auto it = m_map.find(hdl);

		if (it != m_map.end())
			ret = it->second;

		return ret;
	}

	inline bool remove(Key hdl)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		size_t n = m_map.erase(hdl);
		return n > 0;
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_map.clear();
	}

	bool get_first(Key &out_hdl)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_map.size() > 0) {
			out_hdl = m_map.begin()->first;
			return true;
		}

		return false;
	}

	std::mutex &mutex() { return m_mutex; }

	void for_each_obj(cb_func user_cb)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		if (user_cb == nullptr)
			return;
		std::for_each(m_map.begin(), m_map.end(), [user_cb](std::pair<const Key, std::shared_ptr<T>>& pair)
									{
										user_cb(pair.second.get());
									});
	}

private:
	map m_map;
	std::mutex m_mutex;
};

