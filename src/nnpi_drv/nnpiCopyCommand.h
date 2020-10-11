/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include "nnpiInfContext.h"
#include "nnpiDevRes.h"

class nnpiCopyCommand {
public:
	typedef std::shared_ptr<nnpiCopyCommand> ptr;

	static NNPError create(nnpiInfContext::ptr ctx,
			       nnpiDevRes::ptr     devres,
			       nnpiHostRes::ptr    hostres,
			       bool                is_c2h,
			       nnpiCopyCommand::ptr  &outCopy);

	static NNPError create_subres(nnpiDevRes::ptr     devres,
				      nnpiCopyCommand::ptr  &outCopy);

	static NNPError create_d2d(nnpiInfContext::ptr ctx,
				       nnpiDevRes::ptr     dst_devres,
				       nnpiDevRes::ptr     src_devres,
				       nnpiCopyCommand::ptr  &outCopy);
	~nnpiCopyCommand();


	uint16_t id() const { return m_id; }
	bool is_c2h() const { return m_c2h; }
	bool is_d2d() const { return m_is_d2d; }
	uint64_t max_size() const
	{
		if (m_is_d2d) {
			return std::min(m_devres->size(), m_src_devres->size());
		} else {
			return std::min(m_devres->size(), m_hostres->size());
		}
	}
	nnpiInfContext::ptr context() const { return m_ctx; }
	nnpiHostRes::ptr hostres() const { return m_hostres; }

	void set_user_hdl(uint64_t user_hdl) { m_user_hdl = user_hdl; }
	uint64_t user_hdl() const { return m_user_hdl; }


	bool preSchedule()
	{
		if (m_is_d2d)
			return true;

		NNPError ret = m_hostres->lock_device_access(m_c2h);
		if (ret == NNP_NO_ERROR)
			m_scheduled = true;

		return ret == NNP_NO_ERROR;
	}

	bool preSchedule(nnpiHostRes::ptr hostres)
	{
		if (m_is_d2d)
			return true;

		NNPError ret = hostres->lock_device_access(m_c2h);
		if (ret == NNP_NO_ERROR)
			m_scheduled = true;

		return ret == NNP_NO_ERROR;
	}

	void postSchedule(nnpiExecErrorList *error_list = nullptr)
	{
		if (!m_scheduled)
			return;
		else
			m_scheduled = false;

		if (m_is_d2d)
			return;

		if (error_list != nullptr)
			error_list->addFailedHostRes(m_hostres);

		if (m_is_subres) {
			nnpiHostRes::ptr hostres(m_hostres);
			m_hostres.reset();
			hostres->unlock_device_access(m_c2h);
		} else {
			m_hostres->unlock_device_access(m_c2h);
		}
	}

	NNPError schedule(uint64_t size,
			  uint8_t  priority)
	{
		NNPError ret;

		if (!m_ctx->wait_can_schedule())
			return NNP_CONTEXT_BROKEN;

		if (size == 0)
			size = max_size();

		if (m_is_d2d) {
			if (size > m_src_devres->size() || size > m_devres->size())
				return NNP_INVALID_ARGUMENT;

		} else {
			if (size > m_hostres->size() ||
					size > m_devres->size())
				return NNP_INVALID_ARGUMENT;

			if (m_hostres->broken())
				return NNP_HOSTRES_BROKEN;
		}

		if (!preSchedule())
			return NNP_DEVICE_BUSY;

		ret = m_ctx->scheduleCopy(m_id,
					  size,
					  priority);
		if ((!m_is_d2d) && (ret != NNP_NO_ERROR))
			postSchedule();

		return ret;
	}

	NNPError schedule(nnpiHostRes::ptr hostres,
			  uint16_t         hostres_map_id,
			  uint64_t         devres_offset,
			  uint64_t         size)
	{
		NNPError ret;

		if (!m_is_subres)
			return NNP_INVALID_ARGUMENT;

		if (!m_ctx->wait_can_schedule())
			return NNP_CONTEXT_BROKEN;

		if (!hostres.get() ||
		    size > hostres->size() ||
		    devres_offset + size > m_devres->size())
			return NNP_INVALID_ARGUMENT;

		if (!preSchedule(hostres))
			return NNP_DEVICE_BUSY;

		m_hostres = hostres;
		m_hostres_map_id = hostres_map_id;


		ret = m_ctx->scheduleCopySubres(m_id,
						m_hostres_map_id,
						devres_offset,
						size);
		if (ret != NNP_NO_ERROR)
			postSchedule();

		return ret;
	}

	NNPError destroy();

	bool is_need_prepare()
	{
		return m_need_prepare;
	}

private:
	explicit nnpiCopyCommand(nnpiInfContext::ptr ctx,
				 uint16_t            protocol_id,
				 nnpiDevRes::ptr     devres,
				 nnpiHostRes::ptr    hostres,
				 uint16_t            hostres_map_id,
				 bool                is_c2h) :
		m_ctx(ctx),
		m_id(protocol_id),
		m_is_subres(false),
		m_devres(devres),
		m_hostres(hostres),
		m_hostres_map_id(hostres_map_id),
		m_c2h(is_c2h),
		m_user_hdl(0),
		m_need_prepare(true),
		m_is_d2d(false),
		m_scheduled(false)
	{
		if (hostres->usageFlags() & NNP_RESOURECE_USAGE_LOCKLESS)
			m_need_prepare = false;
	}

	explicit nnpiCopyCommand(nnpiInfContext::ptr ctx,
				 uint16_t            protocol_id,
				 nnpiDevRes::ptr     devres) :
		m_ctx(ctx),
		m_id(protocol_id),
		m_is_subres(true),
		m_devres(devres),
		m_hostres_map_id(USHRT_MAX),
		m_c2h(false),
		m_user_hdl(0),
		m_need_prepare(true),
		m_is_d2d(false),
		m_scheduled(false)
	{
	}

	explicit nnpiCopyCommand(nnpiInfContext::ptr ctx,
				 uint16_t            protocol_id,
				 nnpiDevRes::ptr     dst_devres,
				 nnpiDevRes::ptr     src_devres) :
		m_ctx(ctx),
		m_id(protocol_id),
		m_is_subres(false),
		m_devres(dst_devres),
		m_hostres_map_id(USHRT_MAX),
		m_c2h(true),
		m_user_hdl(0),
		m_need_prepare(true),
		m_is_d2d(true),
		m_scheduled(false),
		m_src_devres(src_devres)
	{
	}

	static NNPError update_peers(nnpiDevRes::ptr dst_devres, nnpiDevRes::ptr src_devres);
private:
	nnpiInfContext::ptr m_ctx;
	const uint16_t m_id;
	const bool     m_is_subres;
	nnpiDevRes::ptr m_devres;
	nnpiHostRes::ptr m_hostres;
	uint16_t        m_hostres_map_id;
	bool            m_c2h;
	uint64_t        m_user_hdl;
	bool            m_need_prepare;
	const bool     m_is_d2d;
	bool            m_scheduled;
	nnpiDevRes::ptr m_src_devres;
};
