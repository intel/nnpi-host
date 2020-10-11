/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiUtils.h"
#include <string.h>
#include "safe_lib.h"

void nnpiRingBuffer::push(void *buf, uint32_t size)
{
	uint32_t cont;

	uint8_t *dst = (uint8_t *)lockFreeSpace(size, cont);

	memcpy_s(dst, size, buf, cont);
	if (cont < size)
		memcpy_s(m_buf, size, (uint8_t *)buf + cont, size - cont);

	unlockFreeSpace(size);
}

void nnpiRingBuffer::pop(void *buf, uint32_t size)
{
	uint32_t avail, contSize;

	uint8_t *src = (uint8_t *)lockAvailSpace(size,
						 avail,
						 contSize);

	memcpy_s(buf, size, src, contSize);
	if (contSize < size)
		memcpy_s((uint8_t *)buf + contSize, size - contSize, m_buf, size - contSize);

	unlockAvailSpace(size);
}

int nnpiIDA::alloc(uint32_t &out_id)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_free_ranges.size() < 1)
		return -1;

	std::map<uint32_t, uint32_t>::iterator i = m_free_ranges.begin();

	out_id = (*i).first;
	if ((*i).first < (*i).second)
		m_free_ranges[(*i).first + 1] = (*i).second;

	m_free_ranges.erase(i);

	m_num_alloc++;

	return 0;
}

void nnpiIDA::free(uint32_t id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::map<uint32_t, uint32_t>::iterator i;

	m_num_alloc--;

	for (i = m_free_ranges.begin();
	     i != m_free_ranges.end();
	     i++) {
		if ((*i).first == id + 1) {
			m_free_ranges[id] = (*i).second;
			m_free_ranges.erase(i);
			return;
		} else if ((*i).second == id - 1) {
			(*i).second = id;
			return;
		}
	}

	m_free_ranges[id] = id;
}

uint32_t nnpiIDA::get_num_alloc(void)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	return m_num_alloc;
}
