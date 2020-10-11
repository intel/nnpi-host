/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#ifndef _NNP_DRV_ERROR_H
#define _NNP_DRV_ERROR_H

#define	NNP_ERRNO_UMD_BASE	220	/* NNP errno followed NNP_ERRNO_UMD_BASE, and is complementary linux errno */

#define	NNPER_NO_SUCH_CONTEXT			(NNP_ERRNO_UMD_BASE + 3)    /* No such context */
#define	NNPER_NO_SUCH_NETWORK			(NNP_ERRNO_UMD_BASE + 5)    /* No such network */
#define	NNPER_TOO_MANY_CONTEXTS			(NNP_ERRNO_UMD_BASE + 6)    /* Too many contexts */
#define	NNPER_CONTEXT_BROKEN			(NNP_ERRNO_UMD_BASE + 7)    /* Context broken */
#define	NNPER_TIMED_OUT				(NNP_ERRNO_UMD_BASE + 9)    /* Timed out */
#define	NNPER_BROKEN_MARKER			(NNP_ERRNO_UMD_BASE + 10)   /* Broken marker */
#define	NNPER_NO_SUCH_COPY_HANDLE		(NNP_ERRNO_UMD_BASE + 11)   /* No such copy handle */
#define	NNPER_NO_SUCH_INFREQ_HANDLE		(NNP_ERRNO_UMD_BASE + 12)   /* No such infreq handle */
#define	NNPER_INTERNAL_DRIVER_ERROR		(NNP_ERRNO_UMD_BASE + 13)   /* Internal driver error */
#define	NNPER_NOT_SUPPORTED			(NNP_ERRNO_UMD_BASE + 14)   /* Not supported */
#define	NNPER_INVALID_EXECUTABLE_NETWORK_BINARY	(NNP_ERRNO_UMD_BASE + 15)   /* Invalid exe network binary*/
#define	NNPER_INFER_MISSING_RESOURCE		(NNP_ERRNO_UMD_BASE + 16)   /* Infer missing error */
#define	NNPER_INFER_EXEC_ERROR			(NNP_ERRNO_UMD_BASE + 17)   /* Infer exec error */
#define	NNPER_INFER_SCHEDULE_ERROR		(NNP_ERRNO_UMD_BASE + 18)   /* Infer schedule error */
#define	NNPER_NO_SUCH_CMDLIST			(NNP_ERRNO_UMD_BASE + 19)   /* No such cmd list */
#define	NNPER_ERROR_RUNTIME_LAUNCH		(NNP_ERRNO_UMD_BASE + 20)   /* Error Runtime launch */
#define	NNPER_ERROR_RUNTIME_DIED		(NNP_ERRNO_UMD_BASE + 21)   /* Runtime died */
#define	NNPER_ERROR_OS_CRASHED			(NNP_ERRNO_UMD_BASE + 22)   /* OS crashed */
#define	NNPER_ERROR_EXECUTE_COPY_FAILED		(NNP_ERRNO_UMD_BASE + 23)   /* Execute copy failed */
#define	NNPER_CRITICAL_ERROR_UNKNOWN		(NNP_ERRNO_UMD_BASE + 24)   /* Critical error unknown */
#define	NNPER_HOSTRES_BROKEN			(NNP_ERRNO_UMD_BASE + 25)   /* Hostres broken */
#define NNPER_GRACEFUL_DESTROY                  (NNP_ERRNO_UMD_BASE + 26)   /* Graceful destroy requested by administrator */
#define NNPER_CARD_RESET                        (NNP_ERRNO_UMD_BASE + 27)   /* Card has been reset */
#define NNPER_INCOMPLETE_NETWORK                (NNP_ERRNO_UMD_BASE + 28)   /* Network handle is incomplete */
#define NNPER_INSUFFICIENT_RESOURCES            (NNP_ERRNO_UMD_BASE + 29)   /* Insufficient resources for DevNet */
#define NNPER_ECC_ALLOC_FAILED                  (NNP_ERRNO_UMD_BASE + 30)   /* Insufficient ecc memory on device */
#endif
