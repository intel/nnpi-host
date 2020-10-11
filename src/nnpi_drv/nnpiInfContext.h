/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include "nnpiChannel.h"
#include "ipc_chan_protocol.h"
#include "ipc_c2h_events.h"
#include "nnpdrvInference.h"
#include "nnpiExecErrorList.h"
#include <set>
#include <atomic>
#include <mutex>

NNPError event_valToNNPError(uint32_t event_val);

class nnpiContextObjDB;

class InfContextObjID {
public:
	InfContextObjID(InfContextObjType t,
			int               id = -1,
			int               id2 = -1) :
		m_type(t),
		m_id(id),
		m_id2(id2)
	{
	}

	bool operator<(const InfContextObjID &rhs) const
	{
		if (m_type != rhs.m_type)
			return m_type < rhs.m_type;
		if (m_id != -1 && rhs.m_id != -1 && m_id != rhs.m_id)
			return m_id < rhs.m_id;
		if (m_id2 != -1 && rhs.m_id2 != -1)
			return m_id2 < rhs.m_id2;

		return false;

	}

	bool operator==(const InfContextObjID &rhs) const
	{
		return m_type == rhs.m_type &&
		       (m_id == -1 || rhs.m_id == -1 || m_id == rhs.m_id) &&
		       (m_id2 == -1 || rhs.m_id2 == -1 || m_id2 == rhs.m_id2);
	}

private:
	const InfContextObjType m_type;
	const int               m_id;
	const int               m_id2;
};

class SyncPoint {
public:
	SyncPoint() :
		m_val(0),
		m_wrap(false)
	{
	}

	explicit SyncPoint(uint32_t marker)
	{
		m_val = (uint16_t)(marker & 0xffff);
		m_wrap = ((marker & 0x10000) != 0);
	}

	uint32_t getMarker() const
	{
		uint32_t ret = m_val;

		if (m_wrap)
			ret |= 0x10000;
		return ret;
	}

	uint16_t val() const { return m_val; }

	uint16_t inc()
	{
		if (++m_val == 0)
			m_wrap = !m_wrap;
		return m_val;
	}

	void set(uint16_t val)
	{
		if (val < m_val)
			m_wrap = !m_wrap;
		m_val = val;
	}

	bool operator<(const SyncPoint &rhs)
	{
		if (m_wrap == rhs.m_wrap)
			return m_val < rhs.m_val;
		else
			return m_val > rhs.m_val;
	}

	bool operator>=(const SyncPoint &rhs)
	{
		return !this->operator<(rhs);
	}

private:
	uint16_t m_val;
	bool     m_wrap;
};

class nnpiInfContext {
public:
	typedef std::shared_ptr<nnpiInfContext> ptr;

	static NNPError create(uint32_t             dev_num,
			       uint8_t              flags,
			       nnpiInfContext::ptr &out_ctx);

	~nnpiInfContext();

	NNPError destroy();
	std::mutex &mutex() { return m_mutex; }

	bool broken() const {
		return m_critical_error.value != 0;
	}

	bool aborted() const {
		return m_critical_error.event_code == NNP_IPC_ABORT_REQUEST;
	}

	bool card_fatal() const {
		return m_critical_error.value != 0 &&
			is_card_fatal_drv_event(m_critical_error.event_code);
	}

	NNPError recover();

	nnpiChannel::ptr chan() { return m_chan; }
	nnpiDevice::ptr device() { return m_chan->device(); }
	nnpiContextObjDB *objdb() { return m_objdb; }

	NNPError createDevRes(uint64_t    byteSize,
			      uint32_t    depth,
			      uint64_t    align,
			      uint32_t    usageFlags,
			      uint16_t   &protocol_id,
			      uint64_t   &host_addr,
			      uint8_t    &buf_id);

	NNPError destroyDevRes(uint16_t protocol_id);
	NNPError markDevResDirty(uint16_t protocol_id);
	void freeDevResID(uint16_t protocol_id) {
		m_devres_ida.free(protocol_id);
	}

	NNPError createCopy(uint16_t  devres_protocolID,
			    uint16_t  hostres_map_protocolID,
			    bool      is_c2h,
			    bool      is_subres,
			    uint16_t &out_protocolID);
	NNPError createDeviceToDeviceCopy(uint16_t  src_devres_protocolID,
					  uint64_t  dst_devres_host_addr,
					  uint16_t  dst_devres_protocolID,
					  uint16_t  dst_devres_ctxProtocolID,
					  uint32_t  peer_devID,
					  uint16_t &out_protocolID);
	NNPError destroyCopy(uint16_t protocol_id);
	void freeCopyID(uint16_t protocol_id) {
		m_copy_ida.free(protocol_id);
	}

	NNPError scheduleCopy(uint16_t copy_id,
			      uint64_t byteSize,
			      uint8_t  priority);

	NNPError scheduleCopySubres(uint16_t copy_id,
				    uint16_t hostres_map_id,
				    uint64_t devres_offset,
				    uint64_t byteSize);

	NNPError allocDevNetID(uint16_t &out_protocolID)
	{
		uint32_t id;

		int ret = m_devnet_ida.alloc(id);
		if (ret != 0)
			return NNP_NOT_SUPPORTED;

		out_protocolID = (uint16_t)id;
		return NNP_NO_ERROR;
	}

	void freeDevNetID(uint16_t protocol_id)
	{
		m_devnet_ida.free(protocol_id);
	}

	nnpiIDA &cmdlist_ida() { return m_cmdlist_ida; }

	bool process_create_reply(const union c2h_event_report *ev);

	int send_create_command(const void            *buf,
				size_t                 count,
				const InfContextObjID &id,
				union c2h_event_report &reply);

	int wait_create_command(const InfContextObjID &id,
				union c2h_event_report &reply);

	NNPError createMarker(uint32_t &out_marker);
	NNPError waitMarker(uint32_t marker,
			    uint32_t timeout_us);

	void parseErrorEvent(union c2h_event_report *ev,
			     NNPCriticalErrorInfo  *out_err);

	NNPError sendQueryErrorList(uint32_t cmd_id, bool for_clear = false);
	NNPError waitErrorListQueryCompletion(nnpiExecErrorList *list);
	void parseExecError(nnpiExecErrorList *list,
			    uint32_t idx,
			    NNPCriticalErrorInfo *out_err);
	const nnpiExecErrorList &errorList() const { return m_errorList; };

	NNPError waitCriticalError(NNPCriticalErrorInfo *out_err,
				   uint32_t              timeout_us);
	uint16_t getP2PtransactionID() { return m_p2p_tr++; }

	NNPError trace_user_data(const char *key,
				 uint64_t    user_data);

	NNPError send_user_handle(InfContextObjType type,
		 		  uint16_t id1,
				  uint16_t id2,
				  uint64_t user_handle);

	void set_user_hdl(uint64_t user_hdl) { m_user_hdl = user_hdl; }
	uint64_t user_hdl() const { return m_user_hdl; }

	void cmdlist_finalized_add(int i) {
		m_waitq.update_and_notify([this, i] { m_cmdlist_finalized_in_progress += i; });
	}

	bool wait_can_schedule() {
		m_waitq.wait([this] { return m_cmdlist_finalized_in_progress == 0 || broken(); });
		return !broken();
	}

private:
	explicit nnpiInfContext(nnpiContextObjDB *objdb) :
		m_devres_ida((1 << NNP_IPC_INF_DEVRES_BITS) - 1),
		m_copy_ida((1 << NNP_IPC_INF_COPY_BITS) - 1),
		m_devnet_ida((1 << NNP_IPC_INF_DEVNET_BITS) - 1),
		m_cmdlist_ida((1 << NNP_IPC_INF_CMDS_BITS) - 1),
		m_cmdlist_finalized_in_progress(0),
		m_objdb(objdb),
		m_user_hdl(0)

	{
		m_critical_error.value = 0;
		m_p2p_tr = 0;
	}

	void *lockWriteBuffer()
	{
		uint32_t cont;
		void *ptr;

		ptr = m_cmd_rb->lockFreeSpace(NNP_PAGE_SIZE, cont);

		return ptr;
	}

	void unlockWriteBuffer()
	{
		m_cmd_rb->unlockFreeSpace(NNP_PAGE_SIZE);
	}

	void processExecErrorList(union c2h_ExecErrorList *msg);

	void failAllScheduledCopyCommands();
	void completeAllCommandLists();

	static bool response_handler(const void     *ctx,
				     const uint64_t *response,
				     uint32_t        response_size);

	typedef std::map<InfContextObjID, union c2h_event_report> event_report_map;

private:
	nnpiChannel::ptr m_chan;
	nnpiRingBuffer::ptr m_cmd_rb;
	nnpiRingBuffer::ptr m_resp_rb;
	union c2h_event_report m_critical_error;
	event_report_map m_create_reply;
	nnpiWaitQueue m_waitq;
	nnpiIDA m_devres_ida;
	nnpiIDA m_copy_ida;
	nnpiIDA m_devnet_ida;
	nnpiIDA m_cmdlist_ida;
	uint32_t m_cmdlist_finalized_in_progress;
	nnpiContextObjDB *m_objdb;
	SyncPoint m_sync_point;
	SyncPoint m_last_completed_sync_point;
	std::set<uint16_t> m_failed_sync_points;
	nnpiExecErrorList m_errorList;
	std::mutex m_mutex;
	nnpiInfContext::ptr m_this;  // holds refcount to myself, released by response thread when CONTEXT_DESTROYED command arrived
	std::atomic<uint16_t> m_p2p_tr;
	uint64_t m_user_hdl;
};
