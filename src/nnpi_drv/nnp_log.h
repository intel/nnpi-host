/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include <syslog.h>
#include "log_category_defs.h"

#define NNP_LOG_DEFAULT GENERAL_LOG

#define nnp_log_info(category, msg, ...) \
	syslog(LOG_LOCAL0 | LOG_INFO, category " , INFO, " msg "\n", ##__VA_ARGS__)

#define nnp_log_err(category, msg, ...) \
	syslog(LOG_LOCAL0 | LOG_ERR, category " , ERROR, " msg "\n", ##__VA_ARGS__)

#define nnp_log_warn(category, msg, ...) \
	syslog(LOG_LOCAL0 | LOG_WARNING, category " , WARNING, " msg "\n", ##__VA_ARGS__)

#if defined (_DEBUG) || defined(DEBUG)
#define nnp_log_debug(category, msg, ...) \
	syslog(LOG_LOCAL0 | LOG_DEBUG, category " , DEBUG, " msg "\n", ##__VA_ARGS__)
#else
#define nnp_log_debug(category, msg, ...)
#endif

#ifdef NNP_INTERNAL_LOG
#define nnp_log_internal(NNP_LOG_RT, msg, ...) \
	syslog(LOG_LOCAL0 | LOG_DEBUG, category " , INTERNAL, " msg "\n", ##__VA_ARGS__)
#endif

#define nnp_start_log() \
	openlog(NULL, LOG_PID, LOG_LOCAL0)

#define nnp_end_log() \
	closelog()
