/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "infer_network.h"
#include <string.h>

nnperr_error::nnperr_error(const std::string &where, NNPError err) throw() :
	std::runtime_error(where + std::string(", got NNPError ") + std::to_string(err))
{
}

InferRequest::InferRequest(NNPInferContext context,
			   NNPDeviceNetwork net,
			   void          *configData,
			   uint32_t       configDataSize,
			   uint32_t       maxExecConfigDataSize,
			   bool            is_lockless_hostres,
			   const uint32_t  num_inputs,
			   const uint32_t  num_outputs,
			   const uint64_t *input_sizes,
			   const uint64_t *output_sizes) :
		m_context(context),
		m_in_devres(num_inputs),
		m_out_devres(num_outputs),
		m_in_hostres(num_inputs),
		m_out_hostres(num_outputs),
		m_in_copy(num_inputs),
		m_out_copy(num_outputs),
		m_cmd(-1),
		m_num_scheds(0),
		m_curr_sched(0),
		m_sched_time(NULL),
		m_output_locked_time(NULL)
{
	NNPError err;

	for (unsigned int i = 0; i < num_inputs; i++) {
		err = nnpdrvCreateDeviceResource(context,
						 input_sizes[i],
						 0,
						 NNP_RESOURCE_USAGE_NN_INPUT,
						 &m_in_devres[i]);
		if (err != NNP_NO_ERROR) {
			printf("Error %d when calling nnpdrvCreateDeviceResource\n", err);
			throw nnperr_error(std::string("CreateDeviceResource"), err);
		}

		uint32_t host_usageflags = NNP_RESOURCE_USAGE_NN_INPUT;
		if (is_lockless_hostres)
			host_usageflags |= NNP_RESOURECE_USAGE_LOCKLESS;
		err = nnpdrvCreateHostResource(input_sizes[i],
					       host_usageflags,
					       &m_in_hostres[i]);
		if (err != NNP_NO_ERROR) {
			printf("Error %d when calling nnpdrvCreateHostResource\n", err);
			throw nnperr_error(std::string("CreateHostResource"), err);
		}

		err = nnpdrvCreateHostToDeviceCopyHandle(context,
							 m_in_hostres[i],
							 m_in_devres[i],
							 &m_in_copy[i]);
		if (err != NNP_NO_ERROR) {
			printf("Error %d when calling nnpdrvCreateHostToDeviceCopyHandle\n", err);
			throw nnperr_error(std::string("CreateHostToDeviceCopyHandle"), err);
		}
	}

	for (unsigned int i = 0; i < num_outputs; i++) {
		err = nnpdrvCreateDeviceResource(context,
						 output_sizes[i],
						 0,
						 NNP_RESOURCE_USAGE_NN_OUTPUT,
						 &m_out_devres[i]);
		if (err != NNP_NO_ERROR) {
			printf("Error %d when calling nnpdrvCreateDeviceResource\n", err);
			throw nnperr_error(std::string("CreateDeviceResource"), err);
		}

		uint32_t host_usageflags = NNP_RESOURCE_USAGE_NN_OUTPUT | NNP_RESOURCE_USAGE_NN_INPUT;
		if (is_lockless_hostres)
			host_usageflags |= NNP_RESOURECE_USAGE_LOCKLESS;
		err = nnpdrvCreateHostResource(output_sizes[i],
					       host_usageflags,
					       &m_out_hostres[i]);
		if (err != NNP_NO_ERROR) {
			printf("Error %d when calling nnpdrvCreateHostResource\n", err);
			throw nnperr_error(std::string("CreateHostResource"), err);
		}

		err = nnpdrvCreateDeviceToHostCopyHandle(context,
							 m_out_devres[i],
							 m_out_hostres[i],
							 &m_out_copy[i]);
		if (err != NNP_NO_ERROR) {
			printf("Error %d when calling nnpdrvCreateDeviceToHostCopyHandle\n", err);
			throw nnperr_error(std::string("CreateDeviceToHostCopyHandle"), err);
		}
	}

	err = nnpdrvCreateInferRequest(net,
				       configData,
				       configDataSize,
				       maxExecConfigDataSize,
				       m_in_devres.size(),
				       &m_in_devres[0],
				       m_out_devres.size(),
				       &m_out_devres[0],
				       &m_infreq);

	if (err != NNP_NO_ERROR) {
		 printf("Error %d when calling nnpdrvCreateInferRequest\n", err);
		throw nnperr_error(std::string("CreateInferRequest"), err);
	}
}

InferRequest::~InferRequest()
{
	NNPError err;

	if (m_cmd != (NNPCommandList)-1) {
		err = nnpdrvDestroyCommandList(m_cmd);
		if (err != NNP_NO_ERROR)
			printf("nnpdrvDestroyCommandList returned err: %u", err);
	}

	nnpdrvDestroyInferRequest(m_infreq);

	for (unsigned int i = 0; i < m_in_devres.size(); i++) {
		nnpdrvDestroyCopyHandle(m_in_copy[i]);
		nnpdrvDestroyDeviceResource(m_in_devres[i]);
		nnpdrvDestroyHostResource(m_in_hostres[i]);
	}

	for (unsigned int i = 0; i < m_out_devres.size(); i++) {
		nnpdrvDestroyCopyHandle(m_out_copy[i]);
		nnpdrvDestroyDeviceResource(m_out_devres[i]);
		nnpdrvDestroyHostResource(m_out_hostres[i]);
	}

	if (m_num_scheds) {
		delete [] m_sched_time;
		delete [] m_output_locked_time;
	}
}

void InferRequest::setupPerf(uint32_t num_scheds)
{

	m_num_scheds = num_scheds;
	m_curr_sched = 0;
	m_sched_time = new struct timeval[num_scheds];
	m_output_locked_time = new struct timeval[num_scheds];
	memset(m_sched_time, 0, num_scheds * sizeof(m_sched_time));
	memset(m_output_locked_time, 0, num_scheds * sizeof(m_output_locked_time));
}


InferNetwork::InferNetwork(NNPInferContext context,
			   NNPDeviceNetwork network) :
	m_context(context),
	m_net(network)
{
}

InferNetwork::~InferNetwork()
{
	for (unsigned int i = 0; i < m_requests.size(); i++)
		delete m_requests[i];
	m_requests.clear();
	nnpdrvDestroyDeviceNetwork(m_net);
}

NNPError InferNetwork::createInferRequests(uint32_t       num_requests,
					   void          *configData,
					   uint32_t       configDataSize,
					   uint32_t       maxExecConfigDataSize,
					   bool           is_lockless_hostres,
					   uint32_t       num_inputs,
					   uint32_t       num_outputs,
					   const uint64_t *input_sizes,
					   const uint64_t *output_sizes)
{
	m_requests.resize(num_requests);

	for (unsigned int i = 0; i < num_requests; i++)
		m_requests[i] = new InferRequest(m_context,
						 m_net,
						 configData,
						 configDataSize,
						 maxExecConfigDataSize,
						 is_lockless_hostres,
						 num_inputs,
						 num_outputs,
						 input_sizes,
						 output_sizes);

	return NNP_NO_ERROR;
}
