/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include "nnpiInfContext.h"
#include "nnpiDevRes.h"
#include "nnpiDevNet.h"
#include "nnpdrvInference.h"

class nnpiInfReq {
public:
	typedef std::shared_ptr<nnpiInfReq> ptr;

	static NNPError create(nnpiDevNet::ptr         devnet,
			       const nnpiDevRes::vec  &inputs,
			       const nnpiDevRes::vec  &outputs,
			       const void             *config_data,
			       uint32_t                config_data_size,
			       nnpiInfReq::ptr        &outInfReq);

	~nnpiInfReq()
	{
	}

	uint16_t id() const { return m_id; }

	void set_user_hdl(uint64_t user_hdl) { m_user_hdl = user_hdl; }
	uint64_t user_hdl() const { return m_user_hdl; }

	nnpiDevNet::ptr network() const { return m_devnet; }

	NNPError destroy();
	NNPError schedule(nnpdrvinfSchedParams *schedParams);

private:
	explicit nnpiInfReq(nnpiDevNet::ptr         devnet,
			    uint16_t                protocol_id,
			    const nnpiDevRes::vec  &inputs,
			    const nnpiDevRes::vec  &outputs) :
		m_devnet(devnet),
		m_id(protocol_id),
		m_inputs(inputs),
		m_outputs(outputs),
		m_user_hdl(0)
	{
	}

private:
	nnpiDevNet::ptr m_devnet;
	const uint16_t m_id;
	nnpiDevRes::vec m_inputs;
	nnpiDevRes::vec m_outputs;
	uint64_t m_user_hdl;
};
