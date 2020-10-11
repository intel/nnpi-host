/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiCommandList.h"
#include "nnpiContextObjDB.h"

NNPError nnpiCommandList::create(nnpiInfContext::ptr   ctx,
				 nnpiCommandList::ptr &out_cmdlist)
{
	uint32_t id;

	if (ctx->broken())
		return NNP_CONTEXT_BROKEN;

	int rc = ctx->cmdlist_ida().alloc(id);
	if (rc != 0)
		return NNP_OUT_OF_MEMORY;

	out_cmdlist.reset(new nnpiCommandList((uint16_t)id,
					      ctx));
	ctx->objdb()->insertCommandList(out_cmdlist->id(), out_cmdlist);

	return NNP_NO_ERROR;
}

nnpiCommandList::~nnpiCommandList()
{
	for (uint16_t i = 0; i < m_vec.size(); ++i)
		delete m_vec[i];
	m_vec.clear();
}

NNPError nnpiCommandList::append(nnpiInfCommandSchedParams *sched_cmd)
{
	if (m_finalized)
		return NNP_DEVICE_BUSY;

	if (!sched_cmd)
		return NNP_INVALID_ARGUMENT;

	if (m_vec.size() >= UINT16_MAX)
		return NNP_TOO_MANY_CONTEXTS;

	std::lock_guard<std::mutex> lock(m_waitq.mutex());

	m_vec.push_back(sched_cmd);
	sched_cmd->set_index(m_vec.size() - 1);
	++m_num_edits;

	return NNP_NO_ERROR;
}

nnpiInfCommandSchedParams *nnpiCommandList::get_cmd_for_overwrite(uint16_t usr_idx)
{
	std::lock_guard<std::mutex> lock(m_waitq.mutex());
	uint16_t i;

	for (i = 0; i < m_vec.size() && usr_idx >= m_vec[i]->num_of_subcmds(); ++i)
		usr_idx -= m_vec[i]->num_of_subcmds();

	if (i >= m_vec.size())
		return NULL;

	if (!m_vec[i]->is_edited())
		++m_num_edits;

	return m_vec[i]->get_cmd_for_overwrite(usr_idx);
}

static int get_cmdlist_opt_dependencies(void)
{
	static int val = -1;

	if (val < 0) {
		const char *env_str = getenv("NNPI_CMDLIST_OPT_DEPENDENCIES");
		if (env_str)
			val = atoi(env_str) ? 1 : 0;
		else
			val = 1;
	}

	return val;
}

NNPError nnpiCommandList::send_to_card(uint8_t opcode)
{
	NNPError ret = NNP_NO_ERROR;
	union h2c_ChanInferenceCmdListOp msg;
	uint8_t rb_id = 1; //schedule

	if (m_vec.size() == 0)
		return NNP_NOT_SUPPORTED;

	if (m_context->broken())
		return NNP_CONTEXT_BROKEN;

	msg.value = 0;
	msg.opcode = opcode;
	msg.chan_id = m_context->chan()->id();
	msg.cmdID = m_protocolID;
	msg.destroy = 0; //is not used for sched, zero it anyway
	msg.is_first = 1;
	msg.is_last = 1;
	msg.size = 0;
	if (opcode == NNP_IPC_H2C_OP_CHAN_INF_CMDLIST) {
		msg.opt_dependencies = get_cmdlist_opt_dependencies();
		rb_id = 0; //create
	}

	if (m_num_edits == 0) {
		if (m_context->chan()->write(&msg, sizeof(msg)) != sizeof(msg))
			return NNP_IO_ERROR;
		return NNP_NO_ERROR;
	}

	nnpiRingBuffer::ptr cmd_ring(m_context->chan()->commandRingBuffer(rb_id));

	for (auto it = m_vec.begin(); it != m_vec.end(); ) {
		uint32_t cont;
		uint8_t *ptr = (uint8_t *)cmd_ring->lockFreeSpace(NNP_PAGE_SIZE, cont);
		if (ptr == NULL) {
			ret = NNP_IO_ERROR;
			break;
		} else if (cont != NNP_PAGE_SIZE) {
			cmd_ring->unlockFreeSpace(0);
			ret = NNP_IO_ERROR;
			break;
		}

		uint8_t *p = ptr;
		uint8_t *buf_end = ptr + NNP_PAGE_SIZE;

		if (msg.is_first == 1) {
			*((uint32_t *)p) = (uint32_t)m_num_edits;
			p += 4;
		}

		while (it != m_vec.end() && p < buf_end) {
			if ((*it)->pack(p, buf_end - p))
				++it;
			else
				break;
		}

		msg.size = p - ptr;
		msg.is_last = (it == m_vec.end() ? 1 : 0);

		if (m_context->chan()->write(&msg, sizeof(msg)) != sizeof(msg)) {
			cmd_ring->unlockFreeSpace(0);
			ret = NNP_IO_ERROR;
			break;
		}

		cmd_ring->unlockFreeSpace(NNP_PAGE_SIZE);
		msg.is_first = 0;
	}

	if (ret != NNP_NO_ERROR) {
		for (auto it = m_vec.begin(); it != m_vec.end(); ++it)
			(*it)->clear_edits();
	}
	m_num_edits = 0;

	return ret;
}

nnpiInfCommandSchedParams* nnpiCommandList::getCommand(uint16_t idx)
{
	return m_vec.at(idx);
}

NNPError nnpiCommandList::finalize(uint32_t optFlags)
{
	std::lock_guard<std::mutex> lock(m_waitq.mutex());
	NNPError ret;

	if ((optFlags & BATCH_COPIES) != 0)
		optimize_batch_copies();

	m_context->cmdlist_finalized_add(1);

	ret = send_to_card(NNP_IPC_H2C_OP_CHAN_INF_CMDLIST);
	if (ret != NNP_NO_ERROR) {
		m_context->cmdlist_finalized_add(-1);
		return ret;
	}

	union c2h_event_report reply;
	int rc = m_context->wait_create_command(InfContextObjID(INF_OBJ_TYPE_CMD, m_protocolID),
						reply);
	if (rc != 0)
		return NNP_IO_ERROR;

	if (reply.event_code == NNP_IPC_CREATE_CMD_FAILED)
		return event_valToNNPError(reply.event_val);
	else if (is_context_fatal_event(reply.event_code))
		return NNP_CONTEXT_BROKEN;
	else if (reply.event_code != NNP_IPC_CREATE_CMD_SUCCESS)
		return NNP_IO_ERROR;

	m_context->send_user_handle(INF_OBJ_TYPE_CMD, m_protocolID, 0, m_user_hdl);

	m_finalized = true;

	return ret;
}

NNPError nnpiCommandList::destroy()
{
	std::lock_guard<std::mutex> lock(m_waitq.mutex());
	union h2c_ChanInferenceCmdListOp msg;

	if (!m_finalized) {
		m_context->objdb()->removeCommandList(m_protocolID);
		return NNP_NO_ERROR;
	}

	msg.value = 0;
	msg.opcode = NNP_IPC_H2C_OP_CHAN_INF_CMDLIST;
	msg.chan_id = m_context->chan()->id();
	msg.cmdID = m_protocolID;
	msg.destroy = 1;

	if (m_context->card_fatal())
		return NNP_NO_ERROR;

	if (m_context->chan()->write(&msg, sizeof(msg)) != sizeof(msg))
		return NNP_IO_ERROR;

	return NNP_NO_ERROR;
}

NNPError nnpiCommandList::schedule()
{
	NNPError ret;

	m_waitq.lock();
	if (!m_finalized || m_in_flight ||
	    m_failed_commands > 0 || m_errorList.numErrors() > 0) {
		m_waitq.unlock();
		return NNP_DEVICE_BUSY;
	}

	if (!m_context->wait_can_schedule()) {
		m_waitq.unlock();
		return NNP_CONTEXT_BROKEN;
	}

	m_in_flight = true;
	m_waitq.unlock();

	for (uint16_t i = 0; i < m_vec.size(); ++i)
		if (!m_vec[i]->prepare_schedule()) {
			while (i > 0)
				m_vec[--i]->schedule_done();
			m_waitq.update_and_notify([this]{ m_in_flight = false; });
			return NNP_DEVICE_BUSY;
		}

	ret = send_to_card(NNP_IPC_H2C_OP_CHAN_SCHEDULE_CMDLIST);

	if (ret != NNP_NO_ERROR) {
		for (uint16_t i = 0; i < m_vec.size(); ++i)
			m_vec[i]->schedule_done();
		m_waitq.update_and_notify([this]{ m_in_flight = false; });
	}

	return ret;
}

void nnpiCommandList::complete()
{
	m_waitq.update_and_notify([this]{ m_in_flight = false; });
}

void nnpiCommandList::addError(union c2h_event_report *ev)
{
	std::lock_guard<std::mutex> lock(m_waitq.mutex());

	m_failed_commands++;
}

NNPError nnpiCommandList::clearErrors()
{
	std::lock_guard<std::mutex> lock(m_waitq.mutex());
	NNPError ret;

	if (m_failed_commands == 0 && m_errorList.numErrors() == 0)
		return NNP_NO_ERROR;

	m_errorList.startQuery();
	ret = m_context->sendQueryErrorList(m_protocolID, true);
	if (ret == NNP_NO_ERROR)
		ret = m_context->waitErrorListQueryCompletion(&m_errorList);

	if (ret == NNP_NO_ERROR)
		m_failed_commands = 0;

	return ret;
}

NNPError nnpiCommandList::wait(uint32_t              timeout_us,
			       NNPCriticalErrorInfo *out_errors,
			       uint32_t             *num_errors)
{
	NNPError ret = NNP_NO_ERROR;

	if (!num_errors)
		return NNP_INVALID_ARGUMENT;

	auto cond = [this] {
		bool rc = !m_in_flight || (m_context->broken() && !m_context->aborted());
		return rc;
	};

	bool found;

	if (timeout_us == UINT32_MAX) {
		m_waitq.wait_lock(cond);
		found = true;
	} else {
		found = m_waitq.wait_timeout_lock(timeout_us, cond);
	}

	if (!found)
		ret = NNP_TIMED_OUT;
	else if (m_context->broken())
		ret = NNP_CONTEXT_BROKEN;

	m_waitq.unlock();

	if (ret == NNP_NO_ERROR && m_failed_commands > 0) {
		m_errorList.clear();
		m_errorList.startQuery();
		ret = m_context->sendQueryErrorList(m_protocolID);
		if (ret == NNP_NO_ERROR) {
			ret = m_context->waitErrorListQueryCompletion(&m_errorList);
			if (ret == NNP_NO_ERROR) {
				uint32_t n = std::min(*num_errors, m_errorList.numErrors());
				for (uint32_t i = 0; i < n; i++)
					m_context->parseExecError(&m_errorList, i, &out_errors[i]);
				if (n < *num_errors)
					*num_errors = n;
				else
					*num_errors = m_errorList.numErrors();
			}
		}
	} else {
		*num_errors = 0;
	}

	return ret;
}

void nnpiCommandList::optimize_batch_copies()
{
	nnpiInfCommandSchedParams::vec new_list;
	nnpiInfCopyCommandSchedParams *first_copy_cmd = NULL;
	uint16_t batch_start_idx = 0, new_idx = 0;
	const uint16_t MAX_COPIES_PER_BATCH = 0xffff;

	for (uint16_t i = 0; i < m_vec.size(); i++) {
		bool break_batch = true;

		if (m_vec[i]->type() == CMDLIST_CMD_COPY) {
			nnpiInfCopyCommandSchedParams *copy_cmd = (nnpiInfCopyCommandSchedParams *)m_vec[i];
			/* device to device copy is not part of copy list */
			if(!copy_cmd->copy()->is_d2d()) {
				if (!first_copy_cmd) {
					batch_start_idx = i;
					first_copy_cmd = copy_cmd;
					break_batch = false;
				} else {
					if (copy_cmd->copy()->is_c2h() == first_copy_cmd->copy()->is_c2h() &&
					    (i - batch_start_idx) < MAX_COPIES_PER_BATCH)
						break_batch = false;
				}
			}
		}

		if (first_copy_cmd == NULL) {
			new_list.push_back(m_vec[i]);
			new_list.back()->set_index(new_idx);
			++new_idx;
		} else if (break_batch || (i == m_vec.size() - 1)) {
			uint32_t batch_size = i - batch_start_idx;
			if (!break_batch)
				batch_size++;

			if (batch_size == 1) {
				new_list.push_back(m_vec[batch_start_idx]);
				new_list.back()->set_index(new_idx);
				++new_idx;
			} else {
				nnpiCopyListParams *listp =
					new nnpiCopyListParams((nnpiInfCopyCommandSchedParams**)&m_vec[batch_start_idx],
							       batch_size);
				new_list.push_back(listp);
				new_list.back()->set_index(new_idx);
				++new_idx;
			}
			first_copy_cmd = NULL;

			if (break_batch) {
				new_list.push_back(m_vec[i]);
				new_list.back()->set_index(new_idx);
				++new_idx;
			}
		}
	}

	m_vec.clear();
	m_vec = new_list;
	m_num_edits = m_vec.size();
}
