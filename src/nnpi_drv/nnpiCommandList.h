/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include "nnpiCopyCommand.h"
#include "nnpiInfReq.h"
#include "nnpdrvInference.h"
#include "ipc_chan_protocol.h"
#include <vector>
#include <mutex>
#include <memory>
#include <assert.h>
#include "nnpiExecErrorList.h"

class nnpiInfCommandSchedParams {
public:
	typedef std::vector<nnpiInfCommandSchedParams *> vec;

	virtual ~nnpiInfCommandSchedParams()
	{
	}

	CmdListCommandType type() const { return m_type; }

	virtual bool pack(uint8_t *&ptr, uint32_t size) = 0;
	virtual bool prepare_schedule() = 0;
	virtual void schedule_done(nnpiExecErrorList *error_list = nullptr) = 0;
	virtual void set_index(uint16_t idx) { m_idx = idx; }
	virtual bool is_edited() { return m_edited; }
	virtual void clear_edits() { m_edited = false; }
	virtual uint16_t num_of_subcmds() { return 1; }
	virtual nnpiInfCommandSchedParams *get_cmd_for_overwrite(uint16_t idx)
	{
		if (idx > 0)
			return NULL;

		m_edited = true;

		return this;
	}

protected:
	nnpiInfCommandSchedParams(CmdListCommandType cmdType,
				  uint8_t            priority) :
		m_type(cmdType),
		m_priority(priority),
		m_idx(USHRT_MAX),
		m_edited(true)
	{
	}

	const CmdListCommandType m_type;
	uint8_t                  m_priority;
	uint16_t                 m_idx;
	bool                     m_edited;
};


class nnpiInfCopyCommandSchedParams : public nnpiInfCommandSchedParams {
public:
	nnpiInfCopyCommandSchedParams(nnpiCopyCommand::ptr copy,
				      uint8_t              priority,
				      size_t               size) :
		nnpiInfCommandSchedParams(CMDLIST_CMD_COPY, priority),
		m_copy(copy),
		m_size(std::min(size, copy->max_size()))
	{
	}

	nnpiInfCopyCommandSchedParams(nnpiInfCopyCommandSchedParams &other) :
		nnpiInfCommandSchedParams(CMDLIST_CMD_COPY, other.m_priority),
		m_copy(other.m_copy),
		m_size(other.m_size)
	{
	}

	virtual void overwrite(uint8_t priority, size_t size)
	{
		m_priority = priority;
		m_size = std::min(size, m_copy->max_size());
	}

	virtual ~nnpiInfCopyCommandSchedParams()
	{
	}

	nnpiCopyCommand::ptr copy() { return m_copy; }

	virtual bool pack(uint8_t *&p, uint32_t size)
	{
		if (!m_edited)
			return true;

		if (size < 16)
			return false;

		*((uint32_t *)p) = m_idx; p += 4;
		*((uint8_t *)p) = m_type; p += 1;
		*((uint16_t *)p) = m_copy->id(); p += 2;
		*((uint8_t *)p) = m_priority; p += 1;
		*((uint64_t *)p) = m_size; p += 8;

		m_edited = false;

		return true;
	}

	bool is_need_prepare()
	{
		return m_copy->is_need_prepare();
	}

	virtual bool prepare_schedule()
	{
		return m_copy->preSchedule();
	}

	virtual void schedule_done(nnpiExecErrorList *error_list = nullptr)
	{
		m_copy->postSchedule(error_list);
	}

private:
	nnpiCopyCommand::ptr m_copy;
	size_t               m_size;
};

class nnpiCopyListParams : public nnpiInfCommandSchedParams {
public:
	nnpiCopyListParams(nnpiInfCopyCommandSchedParams **copy_params,
			   uint16_t                        n_copies) :
		nnpiInfCommandSchedParams(CMDLIST_CMD_COPYLIST, 0),
		m_need_prepare(false)
	{
		for (uint16_t i = 0; i < n_copies; ++i) {
			m_copy_params.push_back(copy_params[i]);
			copy_params[i]->set_index(i);
			if (copy_params[i]->is_need_prepare())
				m_need_prepare = true;
		}
		m_num_edits = n_copies;
	}

	virtual ~nnpiCopyListParams()
	{
		for (uint16_t i = 0; i < m_copy_params.size(); i++)
			delete m_copy_params[i];
		m_copy_params.clear();
	}

	virtual bool pack(uint8_t *&p, uint32_t size)
	{
		if (m_edited) {
			assert(m_num_edits > 0);

			if (size < 7)
				return false;

			size -= 7;
			*((uint32_t *)p) = m_idx; p += 4;
			*((uint8_t *)p) = m_type; p += 1;
			*((uint16_t *)p) = (uint16_t)m_num_edits; p += 2;

			m_edited = false;
		}

		if (m_num_edits > 0) {
			for (auto it = m_copy_params.begin(); it != m_copy_params.end(); ++it) {
				uint8_t *start_ptr = p;
				if (!(*it)->pack(p, size))
					return false;
				size -= (p - start_ptr);
			}
			m_num_edits = 0;
		}

		return true;
	}

	virtual bool prepare_schedule()
	{
		bool ret = true;

		if (m_need_prepare) {
			uint16_t i = 0;

			for (; ret && i < m_copy_params.size(); i++)
				ret = m_copy_params[i]->prepare_schedule();

			if (!ret) {
				for (uint16_t j = 0; j < i; j++)
					m_copy_params[j]->schedule_done();
			}
		}

		return ret;
	}

	virtual void schedule_done(nnpiExecErrorList *error_list = nullptr)
	{
		if (m_need_prepare) {
			for (uint16_t i = 0; i < m_copy_params.size(); ++i)
				m_copy_params[i]->schedule_done(error_list);
		}
	}

	virtual void clear_edits()
	{
		if (m_num_edits == 0) {
			assert(!m_edited);
			return;
		}

		m_num_edits = 0;
		m_edited = false;
		for (uint16_t i = 0; i < m_copy_params.size(); ++i)
			m_copy_params[i]->clear_edits();
	}

	virtual uint16_t num_of_subcmds()
	{
		return m_copy_params.size();
	}

	virtual nnpiInfCommandSchedParams *get_cmd_for_overwrite(uint16_t idx)
	{
		if (idx >= num_of_subcmds())
			return NULL;

		if (!m_copy_params[idx]->is_edited()) {
			++m_num_edits;
			m_edited = true;
		}

		return m_copy_params[idx]->get_cmd_for_overwrite(0);
	}

private:
	uint16_t m_num_edits;
	std::vector<nnpiInfCopyCommandSchedParams *> m_copy_params;
	bool m_need_prepare;
};

class nnpiInfReqSchedParams : public nnpiInfCommandSchedParams {
public:
	nnpiInfReqSchedParams(nnpiInfReq::ptr             infreq,
			      const nnpdrvinfSchedParams *schedParams) :
		nnpiInfCommandSchedParams(CMDLIST_CMD_INFREQ,
					  schedParams ? schedParams->priority : 0),
		m_infreq(infreq),
		m_null_params(schedParams == NULL),
		m_batchSize(schedParams ? schedParams->batchSize : 0),
		m_debugOn(schedParams ? schedParams->debugOn : false),
		m_collectInfo(schedParams ? schedParams->collectInfo : false)
	{
	}

	virtual void overwrite(const nnpdrvinfSchedParams *schedParams)
	{
		m_null_params = (schedParams == NULL);
		if (!m_null_params) {
			m_batchSize = schedParams->batchSize;
			m_debugOn = schedParams->debugOn;
			m_collectInfo = schedParams->collectInfo;
		}
	}

	virtual ~nnpiInfReqSchedParams()
	{
	}

	virtual bool pack(uint8_t *&p, uint32_t size)
	{
		if (!m_edited)
			return true;

		if (size < 10 ||
		    (!m_null_params && size < 15))
			return false;

		*((uint32_t *)p) = m_idx; p += 4;
		*((uint8_t *)p) = m_type; p += 1;
		*((uint16_t *)p) = m_infreq->network()->id(); p += 2;
		*((uint16_t *)p) = m_infreq->id(); p += 2;
		*((uint8_t *)p) = m_null_params; p += 1;
		if (!m_null_params) {
			*((uint16_t *)p) = m_batchSize; p += 2;
			*((uint8_t *)p) = m_priority; p += 1;
			*((uint8_t *)p) = m_debugOn; p += 1;
			*((uint8_t *)p) = m_collectInfo; p += 1;
		}

		m_edited = false;

		return true;
	}

	virtual bool prepare_schedule()
	{
		return true;
	}

	virtual void schedule_done(nnpiExecErrorList *error_list = nullptr)
	{
	}

private:
	nnpiInfReq::ptr  m_infreq;
	bool             m_null_params;
	uint16_t         m_batchSize;
	bool             m_debugOn;
	bool             m_collectInfo;
};

class nnpiCommandList {
public:
	typedef std::shared_ptr<nnpiCommandList> ptr;
	enum opt_flags {
		BATCH_COPIES = (1 << 0)
	};

	static NNPError create(nnpiInfContext::ptr   ctx,
			       nnpiCommandList::ptr &out_cmdlist);

	~nnpiCommandList();

	uint16_t id() const { return m_protocolID; }

	NNPError append(nnpiInfCommandSchedParams *sched_cmd);
	nnpiInfCommandSchedParams *get_cmd_for_overwrite(uint16_t usr_idx);
	NNPError finalize(uint32_t optFlags);
	nnpiInfCommandSchedParams* getCommand(uint16_t idx);
	NNPError schedule();
	void addError(union c2h_event_report *ev);
	NNPError clearErrors();

	nnpiExecErrorList *getErrorList() { return &m_errorList; }

	NNPError wait(uint32_t              timeoutUs,
		      NNPCriticalErrorInfo *out_errors,
		      uint32_t             *num_errors);

	void complete();

	NNPError destroy();

	void set_user_hdl(uint64_t user_hdl) { m_user_hdl = user_hdl; }
	uint64_t user_hdl() const { return m_user_hdl; }

protected:
	NNPError send_to_card(uint8_t opcode);

private:
	nnpiCommandList(uint16_t              protocol_id,
			nnpiInfContext::ptr   ctx) :
		m_protocolID(protocol_id),
		m_context(ctx),
		m_finalized(false),
		m_in_flight(false),
		m_num_edits(0),
		m_failed_commands(0),
		m_user_hdl(0)
	{
	}

	void optimize_batch_copies();

private:
	const uint16_t      m_protocolID;
	nnpiInfContext::ptr m_context;
	bool                m_finalized;
	bool                m_in_flight;
	nnpiWaitQueue       m_waitq;
	nnpiInfCommandSchedParams::vec m_vec;
	uint16_t            m_num_edits;
	uint32_t            m_failed_commands;
	nnpiExecErrorList   m_errorList;
	uint64_t            m_user_hdl;
};
