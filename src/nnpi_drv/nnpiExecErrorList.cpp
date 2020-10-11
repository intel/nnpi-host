/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiExecErrorList.h"
#include <stdlib.h>
#include <string.h>
#include "ipc_c2h_events.h"
#include "safe_lib.h"

nnpiExecErrorList::~nnpiExecErrorList()
{
	clearFailedHostRes();
	free(m_buf);
}

void nnpiExecErrorList::clear()
{
	m_desc_vec.clear();
	free(m_buf);
	m_buf = NULL;
	m_size = 0;
	m_buf_size = 0;
	m_complete_eventVal = 0;
	m_state = STATE_CLEARED;
}

void nnpiExecErrorList::startQuery()
{
	m_complete_eventVal = 0;
	m_state = STATE_QUERY_STARTED;
}

void nnpiExecErrorList::completeQuery(nnpiWaitQueue &waitq, uint16_t event_val)
{
	if (event_val == 0) {
		uint32_t pos = 0;
		while (pos < m_size) {
			struct ipc_exec_error_desc *desc = (struct ipc_exec_error_desc *)((uintptr_t)m_buf + pos);

			m_desc_vec.push_back(desc);
			pos += sizeof(struct ipc_exec_error_desc);
			pos += desc->error_msg_size;
		}
	}

	waitq.update_and_notify([this, event_val]{ m_state = STATE_COMPLETED; m_complete_eventVal = event_val; });
}

void nnpiExecErrorList::appendErrorListPacket(nnpiWaitQueue &waitq,
					      void    *packet,
					      uint32_t packet_size,
					      uint32_t total_size,
					      uint16_t error_eventVal)
{
	if (m_state != STATE_QUERY_STARTED)
		return;

	if (packet && packet_size > 0) {
		if (!m_buf) {
			m_buf = malloc(total_size);
			m_buf_size = total_size;
		}

		if (m_buf && m_size + packet_size <= m_buf_size) {
			memcpy_s((void *)((uintptr_t)m_buf + m_size), total_size-m_size, packet, packet_size);
			m_size += packet_size;
			if (m_size == total_size)
				completeQuery(waitq, 0);
		} else
			completeQuery(waitq, NNP_IPC_NO_MEMORY);
	} else {
		completeQuery(waitq, error_eventVal);
	}
}

void nnpiExecErrorList::clearRequestSucceeded(nnpiWaitQueue &waitq)
{
	clear();
	clearFailedHostRes();
	waitq.update_and_notify([this]{ m_state = STATE_COMPLETED; m_complete_eventVal = 0; });
}

NNPError nnpiExecErrorList::getErrorMessage(uint32_t  idx,
					    void     *buf,
					    uint32_t  buf_size,
					    uint32_t *out_buf_size) const
{
	if (idx >= m_desc_vec.size())
		return NNP_INVALID_ARGUMENT;

	struct ipc_exec_error_desc *desc = m_desc_vec[idx];

	if (desc->error_msg_size == 0)
		return NNP_INVALID_ARGUMENT;

	if (out_buf_size)
		*out_buf_size = desc->error_msg_size;

	void *msg_buf = (void *)(desc + 1);
	if (buf && buf_size > 0) {
		if (buf_size >= desc->error_msg_size)
			memcpy_s(buf, buf_size, msg_buf, desc->error_msg_size);
	}

	return NNP_NO_ERROR;
}
