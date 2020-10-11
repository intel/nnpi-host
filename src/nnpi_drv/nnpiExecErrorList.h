/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include <stdint.h>
#include "ipc_chan_protocol.h"
#include "nnpdrvInference.h"
#include "nnpiWaitQueue.h"
#include "nnpiHostProc.h"
#include <vector>
#include <mutex>

class nnpiExecErrorList {
public:
	enum ErrorListState {
		STATE_CLEARED,
		STATE_QUERY_STARTED,
		STATE_COMPLETED
	};

	nnpiExecErrorList() :
		m_buf(NULL),
		m_buf_size(0),
		m_size(0),
		m_complete_eventVal(0),
		m_state(STATE_CLEARED)
	{
	}

	~nnpiExecErrorList();

	bool queryCompleted() const { return m_state == STATE_COMPLETED; }
	uint32_t numErrors() const { return m_desc_vec.size(); }
	uint16_t completionEventVal() const { return m_complete_eventVal; }

	void clear();
	void startQuery();
	void appendErrorListPacket(nnpiWaitQueue &waitq,
				   void    *packet,
				   uint32_t packet_size,
				   uint32_t total_size,
				   uint16_t error_eventVal = 0);

	void clearRequestSucceeded(nnpiWaitQueue &waitq);

	const struct ipc_exec_error_desc *getDesc(uint32_t idx) const {
		if (idx < m_desc_vec.size())
			return m_desc_vec[idx];
		return NULL;
	}

	NNPError getErrorMessage(uint32_t  idx,
				 void     *buf,
				 uint32_t  buf_size,
				 uint32_t *out_buf_size) const;

	void addFailedHostRes(nnpiHostRes::ptr hostRes)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_failed_hostres.push_back(hostRes);
		hostRes->update_copy_fail_count(1);
	}

private:
	void clearFailedHostRes()
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		for (std::vector<nnpiHostRes::ptr>::iterator i = m_failed_hostres.begin();
		     i != m_failed_hostres.end();
		     i++)
			(*i)->update_copy_fail_count(-1);

		m_failed_hostres.clear();
	}

	void completeQuery(nnpiWaitQueue &waitq, uint16_t event_val);

private:
	void          *m_buf;
	uint32_t       m_buf_size;
	uint32_t       m_size;
	uint16_t       m_complete_eventVal;
	ErrorListState m_state;
	std::vector<struct ipc_exec_error_desc *> m_desc_vec;
	std::vector<nnpiHostRes::ptr> m_failed_hostres;
	std::mutex     m_mutex;
};
