/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiHostProc.h"
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <misc/intel_nnpi.h>
#include "nnpiDevice.h"
#include "nnp_log.h"
#include <misc/nnp_error.h>

static pthread_mutex_t s_global_mutex = PTHREAD_MUTEX_INITIALIZER;
nnpiHostProc::weakptr nnpiHostProc::s_theProcHost;

nnpiHandleMap<nnpiHostRes, uint64_t> nnpiHostRes::handle_map;

void nnpiGlobalLock()
{
	pthread_mutex_lock(&s_global_mutex);
}

void nnpiGlobalUnlock()
{
	pthread_mutex_unlock(&s_global_mutex);
}

nnpiHostProc::ptr nnpiHostProc::get()
{
	nnpiGlobalLock();
	nnpiHostProc::ptr sharedPtr = nnpiHostProc::s_theProcHost.lock();

	if (sharedPtr.get() == nullptr) {
		int fd = open("/dev/" NNPDRV_INF_HOST_DEV_NAME, O_RDWR | O_CLOEXEC);
		if (fd != -1) {
			sharedPtr.reset(new nnpiHostProc(fd));
			s_theProcHost = sharedPtr;
		}
	}
	nnpiGlobalUnlock();
	return sharedPtr;
};

void nnpiHostProc::close_host_device()
{
	nnpiGlobalLock();
	nnpiHostProc::ptr proc = nnpiHostProc::s_theProcHost.lock();

	if (proc.get() != nullptr && proc->m_fd >= 0) {
		close(proc->m_fd);
		proc->m_fd = -1;
	}
	nnpiGlobalUnlock();
}

nnpiHostProc::~nnpiHostProc()
{
	if (m_fd >= 0)
		close(m_fd);
}

static inline unsigned int page_shift(void)
{
	static unsigned int pshift;

	if (pshift == 0)
		pshift = ffs(sysconf(_SC_PAGESIZE)) - 1;

	return pshift;
}

int nnpiHostRes::create(uint64_t          byte_size,
			uint32_t          usage_flags,
			nnpiHostRes::ptr &out_hostRes)
{
	struct nnpdrv_ioctl_create_hostres args;
	struct nnpdrv_ioctl_destroy_hostres destroy_args;
	int ret, prot;
	nnpiHostProc::ptr proc(nnpiHostProc::get());

	if (proc->get() == nullptr)
		return ENODEV;

	memset(&args, 0, sizeof(args));
	args.size = byte_size;
	args.usage_flags = usage_flags;

	ret = ioctl(proc->fd(), IOCTL_INF_CREATE_HOST_RESOURCE, &args);
	if (ret < 0)
		return errno;

	prot = (usage_flags & NNP_RESOURCE_USAGE_NN_INPUT  ? PROT_WRITE  : 0) |
	       (usage_flags & NNP_RESOURCE_USAGE_NN_OUTPUT ? PROT_READ : 0);

	nnpiGlobalLock();
	void *mapped_ptr = mmap(NULL, byte_size, prot, MAP_SHARED, proc->fd(), args.user_handle << page_shift());
	if (mapped_ptr == NULL || mapped_ptr == MAP_FAILED) {
		nnpiGlobalUnlock();
		goto err_destroy;
	}
	//set memory mapping to be preserved only for parent process after fork
	ret = madvise(mapped_ptr, byte_size, MADV_DONTFORK);
	if (ret < 0)
		nnp_log_err(CREATE_COMMAND_LOG, "madvise failed with errno: %d.\n", errno);
	nnpiGlobalUnlock();

	out_hostRes.reset( new nnpiHostRes(byte_size,
					   usage_flags,
					   args.user_handle,
					   mapped_ptr,
					   true,
					   proc) );

	return 0;

err_destroy:
	memset(&destroy_args, 0, sizeof(destroy_args));
	destroy_args.user_handle = args.user_handle;

	ioctl(proc->fd(), IOCTL_INF_DESTROY_HOST_RESOURCE, &destroy_args);

	if (errno != 0)
		return errno;

	return EFAULT;
}

int nnpiHostRes::createFromDmaBuf(int               dmaBuf_fd,
				  uint32_t          usage_flags,
				  nnpiHostRes::ptr &out_hostRes)
{
	struct nnpdrv_ioctl_create_hostres args;
	int ret;
	nnpiHostProc::ptr proc(nnpiHostProc::get());

	if (proc->get() == nullptr)
		return ENODEV;

	memset(&args, 0, sizeof(args));
	args.size = 0;
	args.dma_buf = dmaBuf_fd;
	args.usage_flags = usage_flags;

	ret = ioctl(proc->fd(), IOCTL_INF_CREATE_HOST_RESOURCE, &args);
	if (ret < 0)
		return errno;

	out_hostRes.reset( new nnpiHostRes(args.size,
					   dmaBuf_fd,
					   usage_flags,
					   args.user_handle,
					   proc) );

	return 0;
}

int nnpiHostRes::createFromBuf(const void       *buf,
			       uint64_t          byte_size,
			       uint32_t          usage_flags,
			       nnpiHostRes::ptr &out_hostRes)
{
	struct nnpdrv_ioctl_create_hostres args;
	int ret;
	nnpiHostProc::ptr proc(nnpiHostProc::get());

	if (proc->get() == nullptr)
		return ENODEV;

	memset(&args, 0, sizeof(args));
	args.size = byte_size;
	args.usage_flags = usage_flags;
	args.user_handle = (uint64_t)(uintptr_t)buf;

	ret = ioctl(proc->fd(), IOCTL_INF_CREATE_HOST_RESOURCE, &args);
	if (ret < 0)
		return errno;

	out_hostRes.reset( new nnpiHostRes(byte_size,
					   usage_flags,
					   args.user_handle,
					   (void *)buf,
					   false,
					   proc) );

	return 0;
}

NNPError nnpiHostRes::begin_cpu_access()
{
	struct nnpdrv_ioctl_lock_hostres args;
	int ret;

	do {
		memset(&args, 0, sizeof(args));
		args.user_handle = m_kmd_handle;

		ret = ioctl(m_proc->fd(), IOCTL_INF_LOCK_HOST_RESOURCE, &args);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0)
		return nnpiDevice::errnoToNNPError(args.o_errno ?
							args.o_errno : errno);

	return NNP_NO_ERROR;
}

NNPError nnpiHostRes::end_cpu_access()
{
	struct nnpdrv_ioctl_lock_hostres args;
	int ret;

	memset(&args, 0, sizeof(args));
	args.user_handle = m_kmd_handle;

	ret = ioctl(m_proc->fd(), IOCTL_INF_UNLOCK_HOST_RESOURCE, &args);
	if (ret < 0)
		return nnpiDevice::errnoToNNPError(args.o_errno ?
							args.o_errno : errno);

	return NNP_NO_ERROR;
}

NNPError nnpiHostRes::lock_cpu_access(uint32_t timeoutUs, bool for_write)
{
	if (m_usage_flags & NNP_RESOURECE_USAGE_LOCKLESS)
		return NNP_NOT_SUPPORTED;

	NNPError ret = NNP_NO_ERROR;

	bool locked = false;

	// Cannot lock for cpu if already locked for cpu
	if (m_cpu_locked)
		return NNP_INVALID_ARGUMENT;

	auto cond = [this, for_write] {
		return (for_write ? m_readers == 0 : m_readers >= 0);
	};

	if (timeoutUs == UINT32_MAX) {
		m_waitq.wait_lock(cond);
		locked = true;
	} else
		locked = m_waitq.wait_timeout_lock(timeoutUs, cond);

	if (!locked)
		return NNP_TIMED_OUT;

	if (broken()) {
		m_waitq.unlock();
		return NNP_CONTEXT_BROKEN;
	}

	if (m_cpu_sync_needed)
		ret = begin_cpu_access();
	if (ret == NNP_NO_ERROR) {
		if (for_write)
			m_readers = -1;
		else
			m_readers++;

		m_cpu_locked = (for_write ? -1 : 1);
	}

	m_waitq.unlock();

	return ret;
}

NNPError nnpiHostRes::unlock_cpu_access()
{
	if (m_usage_flags & NNP_RESOURECE_USAGE_LOCKLESS)
		return NNP_NOT_SUPPORTED;

	NNPError ret = NNP_NO_ERROR;

	m_waitq.lock();
	if (m_cpu_locked < 0) {
		m_readers = 0;
		m_cpu_locked = 0;
	} else if (m_cpu_locked > 0) {
		m_readers--;
		m_cpu_locked = 0;
	} else
		ret = NNP_INVALID_ARGUMENT;

	if (m_cpu_sync_needed && ret == NNP_NO_ERROR)
		ret = end_cpu_access();

	m_waitq.unlock();

	return ret;
}

NNPError nnpiHostRes::lock_device_access(bool for_write)
{
	if (m_usage_flags & NNP_RESOURECE_USAGE_LOCKLESS)
		return NNP_NO_ERROR;

	NNPError ret = NNP_NO_ERROR;

	m_waitq.lock();
	if (for_write && m_readers == 0)
		m_readers = -1;
	else if (!for_write && m_readers >= 0)
		m_readers++;
	else
		ret = NNP_DEVICE_BUSY;
	m_waitq.unlock();

	return ret;
}

void nnpiHostRes::unlock_device_access(bool for_write)
{
	if (m_usage_flags & NNP_RESOURECE_USAGE_LOCKLESS)
		return;

	m_waitq.update_and_notify([this, for_write] {
					if (for_write)
						m_readers = 0;
					else
						m_readers--;
				  });
}

nnpiHostRes::~nnpiHostRes()
{
	if (m_cpu_addr != NULL && m_mapped)
		munmap(m_cpu_addr, m_byte_size);

	if (m_proc->fd() >= 0) {
		struct nnpdrv_ioctl_destroy_hostres destroy_args;

		memset(&destroy_args, 0, sizeof(destroy_args));
		destroy_args.user_handle = m_kmd_handle;

		int ret = ioctl(m_proc->fd(), IOCTL_INF_DESTROY_HOST_RESOURCE, &destroy_args);
		if (ret < 0)
			nnp_log_err(CREATE_COMMAND_LOG, "Destroy host resource failed with errno: %d, o_errno: %hhu.\n", errno, destroy_args.o_errno);
	}
}
