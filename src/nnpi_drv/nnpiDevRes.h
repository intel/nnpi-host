/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include "nnpiInfContext.h"
#include <vector>

class nnpiDevRes {
public:
	typedef std::shared_ptr<nnpiDevRes> ptr;
	typedef std::vector<nnpiDevRes::ptr> vec;

	static NNPError create(nnpiInfContext::ptr ctx,
			       uint64_t            byteSize,
			       uint32_t            depth,
			       uint64_t            align,
			       uint32_t            usageFlags,
			       nnpiDevRes::ptr    &outDevRes);

	~nnpiDevRes();

	uint8_t buf_id() const { return m_buf_id; }
	uint16_t id() const { return m_id; }
	uint64_t size() const { return m_size; }
	uint32_t usageFlags() const { return m_flags; }
	uint64_t hostAddr() const { return m_host_addr;}

	NNPError destroy()
	{
		return m_ctx->destroyDevRes(m_id);
	}

	NNPError markDirty();
	NNPError d2d_pair(nnpiDevRes::ptr peer);

	void set_user_hdl(uint64_t user_hdl) { m_user_hdl = user_hdl; }
	uint64_t user_hdl() const { return m_user_hdl; }

	nnpiInfContext::ptr m_ctx;
private:
	explicit nnpiDevRes(nnpiInfContext::ptr ctx,
			    uint16_t protocol_id,
			    uint64_t size,
			    uint32_t depth,
			    uint64_t align,
			    uint32_t flags,
			    uint64_t host_addr,
			    uint8_t buf_id) :
		m_ctx(ctx),
		m_id(protocol_id),
		m_size(size),
		m_depth(depth),
		m_align(align),
		m_flags(flags),
		m_host_addr(host_addr),
		m_buf_id(buf_id),
		m_peer(nullptr),
		m_user_hdl(0)
	{
	}

private:
	const uint16_t m_id;
	const uint64_t m_size;
	const uint32_t m_depth;
	const uint64_t m_align;
	const uint32_t m_flags;
	const uint64_t m_host_addr;
	const uint8_t m_buf_id;
	nnpiDevRes::ptr m_peer;
	uint64_t m_user_hdl;
};
