/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiDevRes.h"
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "ipc_chan_protocol.h"
#include "ipc_c2h_events.h"
#include "nnp_log.h"

NNPError nnpiDevRes::create(nnpiInfContext::ptr ctx,
			    uint64_t            byteSize,
			    uint32_t            depth,
			    uint64_t            align,
			    uint32_t            usageFlags,
			    nnpiDevRes::ptr    &outDevRes)
{
	uint16_t protocol_id;
	uint64_t host_addr;
	uint8_t buf_id;
	NNPError ret;

	if (byteSize == 0 || depth == 0 || depth > 255)
		return NNP_INVALID_ARGUMENT;
	if ((align % NNP_PAGE_SIZE) || (align >> (16 + NNP_PAGE_SHIFT)))
		return NNP_NOT_SUPPORTED;

	/* P2P resource can't be source and destination at the same time */
	if ((usageFlags & NNP_RESOURECE_USAGE_P2P_DST) && (usageFlags & NNP_RESOURECE_USAGE_P2P_SRC))
		return NNP_INVALID_ARGUMENT;

	/* a network blob resource cannot be input or output resource */
	if ((usageFlags & NNP_RESOURCE_USAGE_NETWORK) &&
	    (usageFlags & (NNP_RESOURCE_USAGE_NN_INPUT | NNP_RESOURCE_USAGE_NN_OUTPUT)) != 0)
		return NNP_INVALID_ARGUMENT;

	if (ctx->broken())
		return NNP_CONTEXT_BROKEN;

	/* create device resource on device */
	ret = ctx->createDevRes(byteSize,
				depth,
				align,
				usageFlags,
				protocol_id,
				host_addr,
				buf_id);
	if (ret != NNP_NO_ERROR)
		return ret;

	outDevRes.reset(new nnpiDevRes(ctx,
				       protocol_id,
				       byteSize,
				       depth,
				       align,
				       usageFlags,
				       host_addr,
				       buf_id));

	return NNP_NO_ERROR;
}

NNPError nnpiDevRes::markDirty()
{
	/* Only P2P destination resource can be marked */
	if ((m_flags & NNP_RESOURECE_USAGE_P2P_DST) == 0)
		return NNP_INVALID_ARGUMENT;

	return m_ctx->markDevResDirty(m_id);
}
nnpiDevRes::~nnpiDevRes()
{
}
