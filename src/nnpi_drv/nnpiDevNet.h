/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include "nnpiInfContext.h"
#include "nnpiDevRes.h"

class nnpiDevNet {
public:
	typedef std::shared_ptr<nnpiDevNet> ptr;

	static NNPError create(nnpiInfContext::ptr     ctx,
			       const nnpiDevRes::vec  &devres_vec,
			       const void             *config_data,
			       uint32_t                config_data_size,
			       nnpiDevNet::ptr        &outDevnet);

	NNPError add_resources(const nnpiDevRes::vec  &devres_vec,
			       const void             *config_data,
			       uint32_t                config_data_size);

	~nnpiDevNet()
	{
	}

	uint16_t id() const { return m_id; }
	nnpiInfContext::ptr context() { return m_ctx; }
	bool valid() const { return m_devres_vec.size() > 0; }

	void set_user_hdl(uint64_t user_hdl) { m_user_hdl = user_hdl; }
	uint64_t user_hdl() const { return m_user_hdl; }

	NNPError setProperty(NNPNetPropertiesType property,
			uint32_t property_val,
			uint32_t timeout_us);

	NNPError destroy();

	NNPError allocInfReqID(uint16_t &out_protocolID)
	{
		uint32_t id;

		int ret = m_infreq_ida.alloc(id);
		if (ret != 0)
			return NNP_NOT_SUPPORTED;

		out_protocolID = (uint16_t)id;
		return NNP_NO_ERROR;
	}

	void freeInfReqID(uint16_t protocol_id)
	{
		m_infreq_ida.free(protocol_id);
	}

private:
	explicit nnpiDevNet(nnpiInfContext::ptr     ctx,
			    uint16_t                protocol_id,
			    const nnpiDevRes::vec  &devres_vec) :
		m_ctx(ctx),
		m_id(protocol_id),
		m_infreq_ida((1 << NNP_IPC_INF_REQ_BITS) - 1),
		m_devres_vec(devres_vec),
		m_user_hdl(0)
	{
	}

private:
	nnpiInfContext::ptr m_ctx;
	const uint16_t m_id;
	nnpiIDA m_infreq_ida;
	nnpiDevRes::vec m_devres_vec;
	uint64_t m_user_hdl;
};
