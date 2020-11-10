/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

/**
 * @brief Inference UMD test - tests infer request creation, destruction and execution.
 * @file dummy_inference.cpp
 *
 * This program uses the special NNP_ULT_CONTEXT feature which instruct the
 * NNP-I device to ignore the inference network definition and for every
 * inference execution, it only copies the content from input resource(s)
 * to output resource(s).
 * This allows to exercise the entire flow of sending inference work and data
 * to the device without using any special inference network definition.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <thread>
#include "nnpdrvInference.h"
#include "infer_network.h"

static unsigned int s_num_requests = 1;
static unsigned int s_num_sched_per_infreq = 1;
static bool s_quiet = false;

static uint64_t blob_size = 2 * 1024 * 1024;
static uint64_t in_resource_size = 1024 * 1024;
static uint64_t out_resource_size = 1024 * 1024;
static uint64_t cmp_resource_size = 1024 * 1024;
static uint64_t s_copy_size = 0;
static uint64_t partCopy;
static uint64_t *i_size ;
static uint64_t *o_size ;

uint32_t device_num = 0;
static uint32_t num_of_inf_inputs_outputs = 1;
static bool use_cmd_list = false;
static bool cmd_list_no_h2c = false;

static std::vector<char> sched_config;
static nnpdrvinfSchedParams schedParams;

#define INTERRUPT_RETRIES 1

#define ASSERT_NO_THROW(cmd)  \
{ \
	try { \
		(cmd); \
	} \
	catch(...) { \
		fprintf(stderr, "Assert on line %d\n", __LINE__); \
		return -1; \
	} \
}

#define ASSERT_EQ(lhs,rhs) \
	if ((lhs) != (rhs)) { \
		fprintf(stderr, "Line %d " #lhs "(%d) != " #rhs "(%d)\n", __LINE__, (lhs), (rhs)); \
		return -1; \
	}

int run_dummy_inference(void)
{
	NNPInferContext context;
	NNPDeviceNetwork net;
	InferNetwork *network;
	NNPError err;
	void **in_vptr;
	uint32_t input_idx;
	void *out_vptr;
	char *init_buf = new char[out_resource_size];

	i_size = new uint64_t [num_of_inf_inputs_outputs];
	o_size = new uint64_t [num_of_inf_inputs_outputs];
	for (uint32_t i = 0; i< num_of_inf_inputs_outputs; i++) {
		i_size[i] = in_resource_size;
		o_size[i] = out_resource_size;
	}

	//////////////////////////////////////////////////////////////
	// Setup
	//////////////////////////////////////////////////////////////

	// Create infer context on the selected NNP-I device number
	printf("Creating ULT_CONTEXT infer context on device %u\n", device_num);
	err = nnpdrvCreateInferContextWithFlags(device_num, NNP_ULT_CONTEXT, &context);
	ASSERT_EQ(NNP_NO_ERROR, err);

	// Create device network
	//
	// Here we should create one or more device resources and load them
	// with network blob data which is the output of the graph compiler.
	// Since this is a dummy ULT context, this netowrk data is not used
	// by the device. We just create a device network object with one
	// single resource that we initialize with some garbadge value.
	//
	printf("Creating infer network \n");
	NNPDeviceResource blob_devres;

	char *blob_buf = new char[blob_size];
	memset(blob_buf, 0xaa, blob_size);

	// Create the device resource to hold the network blob
	err = nnpdrvCreateDeviceResource(context,
					 blob_size,
					 0,
					 NNP_RESOURCE_USAGE_NETWORK,
					 &blob_devres);
	ASSERT_EQ(NNP_NO_ERROR, err);

	// Load the device resource with the (garbadge) network data
	err = nnpdrvDeviceResourceSubLoad(blob_devres,
					  0,
					  blob_buf,
					  blob_size);
	ASSERT_EQ(NNP_NO_ERROR, err);

	delete [] blob_buf;

	// Create the device network object
	err = nnpdrvCreateDeviceNetworkWithResources(context,
						     &blob_devres, 1,
						     NULL, 0,
						     &net);
	ASSERT_EQ(NNP_NO_ERROR, err);

	ASSERT_NO_THROW(network = new InferNetwork(context, net));

	//
	// The handle to device resource of network blob is not
	// needed any more since the network object has reference to
	// it.
	//
	nnpdrvDestroyDeviceResource(blob_devres);

	//
	// Create Infer requests executors, each hold a dedicated
	// set of host and device resource(s) for the network input
	// and output resources.
	//
	printf("Creating infer requests\n");
	ASSERT_NO_THROW(err = network->createInferRequests(s_num_requests,
							  NULL, 0,
							  0,
							  false,
							  num_of_inf_inputs_outputs,
							  num_of_inf_inputs_outputs,
							  i_size,
							  o_size));
	ASSERT_EQ(NNP_NO_ERROR, err);

	in_vptr = NULL;
	memset(init_buf, 0xdd, out_resource_size);

	//////////////////////////////////////////////////////////////////////
	// Setup DONE - Inference execution starts here
	//////////////////////////////////////////////////////////////////////

	//
	// For each infer request executor:
	//    - initialize input resources
	//    - initilize output resource with garbadge data
	//    - execute the infer request for the first time
	//
	if (!s_quiet)
		printf("Locking and initializing input host resource\n");
	InferRequestVec &reqs = network->getInferRequests();
	if (in_vptr == NULL)
		in_vptr = new void*[num_of_inf_inputs_outputs * reqs.size()];
	for (unsigned int i = 0; i < reqs.size(); i++) {
		for (unsigned int res_idx = 0; res_idx < num_of_inf_inputs_outputs; res_idx++) {
			// Lock and initialize input host resource
			input_idx = (i*num_of_inf_inputs_outputs) + res_idx;
			err = nnpdrvGetHostResourceCPUAddress(reqs[i]->getInputResources()[res_idx], &in_vptr[input_idx]);
			ASSERT_EQ(NNP_NO_ERROR, err);
			err = nnpdrvLockHostResource(reqs[i]->getInputResources()[res_idx], UINT32_MAX);
			ASSERT_EQ(NNP_NO_ERROR, err);
			memset(in_vptr[input_idx], 0x5a + i, in_resource_size / 2);
			memset((char *)in_vptr[input_idx] + in_resource_size / 2,
			       0xa5 + i, in_resource_size / 2);
			err = nnpdrvUnlockHostResource(reqs[i]->getInputResources()[res_idx]);
			ASSERT_EQ(NNP_NO_ERROR, err);

			// Lock and initialize output host resource with dummy
			// content
			err = nnpdrvGetHostResourceCPUAddress(reqs[i]->getOutputResources()[res_idx], &out_vptr);
			ASSERT_EQ(NNP_NO_ERROR, err);

			err = nnpdrvLockHostResource(reqs[i]->getOutputResources()[res_idx], UINT32_MAX);
			ASSERT_EQ(NNP_NO_ERROR, err);

			memset(out_vptr, 0xdd, out_resource_size);
			err = nnpdrvUnlockHostResource(reqs[i]->getOutputResources()[res_idx]);
			ASSERT_EQ(NNP_NO_ERROR, err);
		}

		//
		// Schedule the infer request executor for the fitst time
		//
		if (!s_quiet)
			printf("Scheduling copy+infer+copy operations for infer#%u\n", i);
		if (use_cmd_list) {
			err = reqs[i]->createCmdList(NULL, cmd_list_no_h2c);
			ASSERT_EQ(NNP_NO_ERROR, err);
			err = reqs[i]->scheduleCmdList(s_copy_size);
		} else {
			err = reqs[i]->schedule(NULL, s_copy_size);
		}

		ASSERT_EQ(NNP_NO_ERROR, err);
	}

	//
	// For each infer request executor:
	//    - map output host resource and validate infer results.
	//    - start the next execution until number of requested scheduled
	//      has reached
	//
	for (unsigned int s = 0; s < s_num_sched_per_infreq; s++) {
		InferRequestVec &reqs = network->getInferRequests();
		for (unsigned int i = 0; i < reqs.size(); i++) {
			for (unsigned int res_idx = 0; res_idx < num_of_inf_inputs_outputs; res_idx++) {
				input_idx = (i*num_of_inf_inputs_outputs) + res_idx;

				//
				// Lock output host resource and get its CPU
				// address
				//
				if (!s_quiet)
					printf("Locking and checking output host resource#%u\n", res_idx);
				err = nnpdrvGetHostResourceCPUAddress(reqs[i]->getOutputResources()[res_idx], &out_vptr);
				ASSERT_EQ(NNP_NO_ERROR, err);

				err = nnpdrvLockHostResource(reqs[i]->getOutputResources()[res_idx], UINT32_MAX);
				ASSERT_EQ(NNP_NO_ERROR, err);
				reqs[i]->outputLocked();

				//
				// Validate output content.
				// Data should match the inout resource content
				// as the dummy ULT context will copy inputs to
				// outputs.
				// If partial copy is done, check that no more
				// than the requested size has been copied.
				//
				ASSERT_EQ(0, memcmp(in_vptr[input_idx], out_vptr, partCopy));
				ASSERT_EQ(0, memcmp((char *)out_vptr + partCopy, init_buf + partCopy, out_resource_size - partCopy));

				// Unlock the output resource
				err = nnpdrvUnlockHostResource(reqs[i]->getOutputResources()[res_idx]);
				ASSERT_EQ(NNP_NO_ERROR, err);
			}

			//
			// Schedule the next execution of the infer request, if
			// not yet executed enough times
			//
			if (s < s_num_sched_per_infreq-1) {
				if (!s_quiet)
					printf("scheduling infer request\n");
				if (use_cmd_list)
					err = reqs[i]->scheduleCmdList(s_copy_size);
				else
					err = reqs[i]->schedule(NULL, s_copy_size);
				ASSERT_EQ(NNP_NO_ERROR, err);
			}
		}
	}

	printf("\n\nTest has PASSED\n\n");

	printf("Destroying all resources\n");

	delete network;
	delete [] in_vptr;
	delete [] init_buf;

	// Destroy the infer context
	err = nnpdrvDestroyInferContext(context);
	ASSERT_EQ(NNP_NO_ERROR, err);

	delete [] i_size;
	delete [] o_size;

	return 0;
}

void printUsage()
{
	printf("dummy_inference [options]\n");
	printf("\n");
	printf("-nreq <num>  - num of inference request objects (default: 1)\n");
	printf("-nsched <num> - number of schedule for each infer request object (default 1)\n");
	printf("-res_size <num>[,<num] - byte size of input/output resource (default 1MB)\n");
	printf("-copy_size <num> - byte size to copy, should be less or equal to \"res_size\" (default equal to \"res_size\")\n");
	printf("-d_id <num>  - Device id. Used for execute on specific device\n");
	printf("-num_io <num>  - num of inputs/outputs (default: 1)\n");
	printf("-cmd_list - use Command List API to schedule copy and ifer requests\n");
}

int main(int argc, char *argv[])
{
	unsigned int argn = argc;

	for (unsigned int i = 1; i < argn; ++i) {
		if (!strcmp(argv[i], "-nreq") && i+1 < argn) {
			s_num_requests = strtoul(argv[++i], NULL, 0);
			if (s_num_requests == 0) {
				std::cout << "Number of requests cannot be zero" << std::endl;
				return -1;
			}
		} else if (!strcmp(argv[i], "-d_id") && i+1 < argn) {
			device_num = strtoul(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "-nsched") && i+1 < argn) {
			s_num_sched_per_infreq = strtoul(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "-res_size") && i+1 < argn) {
			int n;
			uint64_t isize, osize;

			n = sscanf(argv[++i], "%lu,%lu", &isize, &osize);
			if (n == 1) {
				in_resource_size = isize;
				out_resource_size = isize;
				cmp_resource_size = isize;
			} else if (n == 2) {
				in_resource_size = isize;
				out_resource_size = osize;
				cmp_resource_size = std::min(isize, osize);
			} else {
				printf("Failed to parse -res_size\n\n");
				printUsage();
				return -1;
			}
		} else if (!strcmp(argv[i], "-copy_size") && i+1 < argn) {
			s_copy_size = strtoull(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "-num_io")) {
			num_of_inf_inputs_outputs = strtoul(argv[++i], NULL, 0);
			if (num_of_inf_inputs_outputs == 0)
				num_of_inf_inputs_outputs = 1;
		} else if (!strcmp(argv[i], "-cmd_list")) {
			use_cmd_list = true;
		} else if (!strcmp(argv[i], "-cmd_list_no_h2c")) {
			use_cmd_list = true;
			cmd_list_no_h2c = true;
		} else {
			printUsage();
			return -1;
		}
	}

	if (s_copy_size > 0 && s_copy_size < cmp_resource_size)
		cmp_resource_size = s_copy_size;

	if (s_copy_size > cmp_resource_size)
		s_copy_size = cmp_resource_size;

	partCopy = s_copy_size == 0 ? cmp_resource_size : s_copy_size;

	return run_dummy_inference();
}
