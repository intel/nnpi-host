/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include "nnpdrvInference.h"
#include <vector>
#include <stdexcept>
#include <sys/time.h>
#include <stdio.h>
#include <string>

class nnperr_error : public std::runtime_error
{
public:
	explicit nnperr_error(const std::string &where, /*NNP_IGNORE_STYLE_CHECK*/
			      NNPError err) throw();
};

typedef std::vector<NNPHostResource> HostResourceVec;
typedef std::vector<NNPDeviceResource> DeviceResourceVec;
typedef std::vector<NNPCopyHandle> CopyHandleVec;

class InferRequest
{
public:
	InferRequest(NNPInferContext context,
		     NNPDeviceNetwork net,
		     void          *configData,
		     uint32_t       configDataSize,
		     uint32_t       maxExecConfigDataSize,
		     bool            is_lockless_hostres,
		     uint32_t        num_inputs,
		     uint32_t        num_outputs,
		     const uint64_t *input_sizes,
		     const uint64_t *output_sizes);

	~InferRequest();

	HostResourceVec &getInputResources() /*NNP_IGNORE_STYLE_CHECK*/
	{
		return m_in_hostres;
	}

	HostResourceVec &getOutputResources() /*NNP_IGNORE_STYLE_CHECK*/
	{
		return m_out_hostres;
	}

	NNPError createCmdList(nnpdrvinfSchedParams *schedParams, bool exclude_h2c = false)
	{
		NNPError err;

		if (m_cmd != (NNPCommandList)-1)
			return NNP_NO_ERROR;

		err = nnpdrvCreateCommandListBegin(m_context, &m_cmd);
		if (err != NNP_NO_ERROR) {
			printf("nnpdrvCreateCommandListBegin returned err: %u\n", err);
			return err;
		}
		m_cmd_no_h2c = exclude_h2c;
		if (!m_cmd_no_h2c) {
			for (unsigned int i = 0; i < m_in_copy.size(); ++i) {
				err = nnpdrvCommandListAppendCopy(m_cmd, m_in_copy[i], 0, 0, 0);
				if (err != NNP_NO_ERROR) {
					printf("nnpdrvCommandListAppendCopy (in %u) returned err: %u\n", i, err);
					return err;
				}
			}
		}
		err = nnpdrvCommandListAppendInferRequest(m_cmd, m_infreq, schedParams);
		if (err != NNP_NO_ERROR) {
			printf("nnpdrvCommandListAppendCopy (in) returned err: %u\n", err);
			return err;
		}
		for (unsigned int i = 0; i < m_out_copy.size(); ++i) {
			err = nnpdrvCommandListAppendCopy(m_cmd, m_out_copy[i], 0, 0, 0);
			if (err != NNP_NO_ERROR) {
				printf("nnpdrvCommandListAppendCopy (out %u) returned err: %u\n", i, err);
				return err;
			}
		}
		err = nnpdrvCreateCommandListEnd(m_cmd);
		if (err != NNP_NO_ERROR) {
			printf("nnpdrvCreateCommandListEnd returned err: %u\n", err);
			return err;
		}

		return NNP_NO_ERROR;
	}

	NNPError waitCmdList(void)
	{
		NNPCriticalErrorInfo error;
		uint32_t num = 1;
		NNPError err = nnpdrvWaitCommandList(m_cmd, UINT32_MAX, &error, &num);
		if (num > 0) {
			printf("Got error in command list exec: nnpCriticalError=%d objType=%d\n", error.nnpCriticalError, error.objType);
			return NNP_CONTEXT_BROKEN;
		}
		return err;
	}

	NNPError scheduleCmdList(uint64_t partialSize = 0)
	{
		NNPError err;

		if (m_num_scheds)
			gettimeofday(&m_sched_time[m_curr_sched], NULL);

		if (partialSize != 0) {
			uint32_t c2h_start = 1;

			if (!m_cmd_no_h2c) {
				for (unsigned int i = 0; i < m_in_copy.size(); ++i) {
					err = nnpdrvCommandListOverwriteCopy(m_cmd, i, partialSize, 0, 0);
					if (err != NNP_NO_ERROR) {
						printf("nnpdrvCommandListOverwriteCopy returned err: %u\n", err);
						return err;
					}
				}
				c2h_start += m_in_copy.size();
			}

			for (unsigned int i = 0; i < m_out_copy.size(); ++i) {
				err = nnpdrvCommandListOverwriteCopy(m_cmd, c2h_start + i, partialSize, 0, 0);
				if (err != NNP_NO_ERROR) {
					printf("nnpdrvCommandListOverwriteCopy returned err: %u\n", err);
					return err;
				}
			}
		}

		if (m_cmd_no_h2c) {
			for (unsigned int i = 0; i < m_in_copy.size(); ++i)
				if (partialSize != 0)
					nnpdrvScheduleCopy(m_in_copy[i], partialSize, 0);
				else
					nnpdrvScheduleCopy(m_in_copy[i], 0, 0);
		}

		err = nnpdrvScheduleCommandList(m_cmd);
		if (err != NNP_NO_ERROR) {
			printf("nnpdrvScheduleCommandList returned err: %u\n", err);
			return err;
		}

		return NNP_NO_ERROR;
	}

	NNPError schedule(nnpdrvinfSchedParams *schedParams, uint64_t partCopy = 0)
	{
		NNPError err;

		if (m_num_scheds)
			gettimeofday(&m_sched_time[m_curr_sched], NULL);

		for (unsigned int i = 0; i < m_in_copy.size(); i++) {
			err = nnpdrvScheduleCopy(m_in_copy[i], partCopy, 0);
			if (err != NNP_NO_ERROR)
				return err;
		}

		err = nnpdrvScheduleInferReq(m_infreq, schedParams);
		if (err != NNP_NO_ERROR)
			return err;

		for (unsigned int i = 0; i < m_out_copy.size(); i++) {
			err = nnpdrvScheduleCopy(m_out_copy[i], partCopy, 0);
			if (err != NNP_NO_ERROR)
				return err;
		}

		return NNP_NO_ERROR;
	}

	NNPError finish(void)
	{
		return nnpdrvFinish(m_context);
	}


	void setupPerf(uint32_t num_scheds);

	void outputLocked(void)
	{
		if (m_num_scheds) {
			gettimeofday(&m_output_locked_time[m_curr_sched], NULL);
			m_curr_sched++;
		}
	}

	uint64_t schedTime(uint32_t idx)
	{
		if (!m_sched_time)
			return 0;

		return (m_sched_time[idx].tv_sec * 1000000) +
			m_sched_time[idx].tv_usec;
	}

	uint64_t outputLockedTime(uint32_t idx)
	{
		if (!m_output_locked_time)
			return 0;

		return (m_output_locked_time[idx].tv_sec * 1000000) +
			m_output_locked_time[idx].tv_usec;
	}

	void printPerf(FILE *fp, uint32_t idx)
	{
		if (!m_sched_time || !m_output_locked_time)
			return;

		uint64_t sched_time = schedTime(idx);
		uint64_t locked_time = outputLockedTime(idx);

		fprintf(fp, "%lu,%lu,%ld\n", sched_time, locked_time, locked_time - sched_time);
	}

private:
	InferRequest(NNPInferContext context,
		     const uint32_t num_inputs,
		     const uint32_t num_outputs) :
		m_context(context),
		m_in_devres(num_inputs),
		m_out_devres(num_outputs),
		m_in_hostres(num_inputs),
		m_out_hostres(num_outputs),
		m_in_copy(num_inputs),
		m_out_copy(num_outputs),
		m_cmd(-1)
	{};

private:
	NNPInferContext   m_context;
	DeviceResourceVec m_in_devres;
	DeviceResourceVec m_out_devres;
	HostResourceVec   m_in_hostres;
	HostResourceVec   m_out_hostres;
	CopyHandleVec     m_in_copy;
	CopyHandleVec     m_out_copy;
	NNPInferRequest   m_infreq;
	NNPCommandList    m_cmd;
	bool              m_cmd_no_h2c;
	int               m_num_scheds;
	int               m_curr_sched;
	struct timeval    *m_sched_time;
	struct timeval    *m_output_locked_time;
};

typedef std::vector<InferRequest *> InferRequestVec; /*NNP_IGNORE_STYLE_CHECK*/

class InferNetwork
{
public:
	InferNetwork(NNPInferContext context,
		     NNPDeviceNetwork network);

	~InferNetwork();

	NNPError createInferRequests(uint32_t       num_requests,
				     void          *configData,
				     uint32_t       configDataSize,
				     uint32_t       maxExecConfigDataSize,
				     bool           is_lockless_hostres,
				     uint32_t       num_inputs,
				     uint32_t       num_outputs,
				     const uint64_t *input_sizes,
				     const uint64_t *output_sizes);

	InferRequestVec &getInferRequests() /*NNP_IGNORE_STYLE_CHECK*/
	{
		return m_requests;
	}

private:
	NNPInferContext  m_context;
	InferRequestVec  m_requests;
	NNPDeviceNetwork m_net;
};
