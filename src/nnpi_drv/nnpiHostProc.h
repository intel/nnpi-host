/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#pragma once

#include <stdint.h>
#include <memory>
#include <atomic>
#include "nnpdrvInference.h"
#include "nnpiWaitQueue.h"
#include "nnpiHandleMap.h"

extern "C" {
	void nnpiGlobalLock();
	void nnpiGlobalUnlock();
}

class nnpiHostProc {
public:
	typedef std::shared_ptr<nnpiHostProc> ptr;
	typedef std::weak_ptr<nnpiHostProc> weakptr;

	static nnpiHostProc::ptr get();
	static void close_host_device();

	~nnpiHostProc();

	inline int fd() const { return m_fd; }

private:
	explicit nnpiHostProc(int fd) :
		m_fd(fd)
	{
	}

private:
	static nnpiHostProc::weakptr s_theProcHost;
	int m_fd;
};

class nnpiHostRes {
public:
	typedef std::shared_ptr<nnpiHostRes> ptr;

	static int create(uint64_t          byte_size,
			  uint32_t          usage_flags,
			  nnpiHostRes::ptr &out_hostRes);

	static int createFromBuf(const void       *buf,
				 uint64_t          byte_size,
				 uint32_t          usage_flags,
				 nnpiHostRes::ptr &out_hostRes);

	~nnpiHostRes();

	static nnpiHandleMap<nnpiHostRes, uint64_t> handle_map;

	NNPError lock_cpu_access(uint32_t timeoutUs, bool for_write);
	NNPError unlock_cpu_access();
	NNPError lock_device_access(bool for_write);
	void unlock_device_access(bool for_write);

	bool allocated() const { return m_allocated; }
	int  dmaBuf_fd() const { return m_dmaBuf_fd; }
	uint64_t size() const { return m_byte_size; }
	int kmd_handle() const { return m_kmd_handle; }
	void *vaddr() const { return m_cpu_addr; }
	uint32_t usageFlags() const { return m_usage_flags; }
	void enableCPUsync() { m_cpu_sync_needed = true; }

	void update_copy_fail_count(int n)
	{
		std::atomic_fetch_add(&m_failed_copy_ops, n);
	}
	bool broken() const { return std::atomic_load(&m_failed_copy_ops) > 0; }

	void set_user_hdl(uint64_t user_hdl) { m_user_hdl = user_hdl; }
	uint64_t get_user_hdl() { return m_user_hdl; }

private:
	nnpiHostRes(uint64_t byte_size,
		    uint32_t usage_flags,
		    int      kmd_handle,
		    void    *mappedAddr,
		    bool     alloced,
		    const nnpiHostProc::ptr &proc) :
		m_allocated(true),
		m_dmaBuf_fd(-1),
		m_usage_flags(usage_flags),
		m_byte_size(byte_size),
		m_kmd_handle(kmd_handle),
		m_failed_copy_ops(0),
		m_cpu_addr(mappedAddr),
		m_alloced(alloced),
		m_proc(proc),
		m_readers(0),
		m_cpu_locked(0),
		m_cpu_sync_needed(false),
		m_user_hdl(0)
	{
	}

	nnpiHostRes(uint64_t byte_size,
		    int      dmaBuf_fd,
		    uint32_t usage_flags,
		    int      kmd_handle,
		    const nnpiHostProc::ptr &proc) :
		m_allocated(false),
		m_dmaBuf_fd(dmaBuf_fd),
		m_usage_flags(usage_flags),
		m_byte_size(byte_size),
		m_kmd_handle(kmd_handle),
		m_failed_copy_ops(0),
		m_cpu_addr(NULL),
		m_alloced(false),
		m_proc(proc),
		m_readers(0),
		m_cpu_locked(0),
		m_cpu_sync_needed(true),
		m_user_hdl(0)
	{
	}

	NNPError begin_cpu_access();
	NNPError end_cpu_access();

private:
	const bool       m_allocated;
	const int        m_dmaBuf_fd;
	const uint32_t   m_usage_flags;
	const uint64_t   m_byte_size;
	const int        m_kmd_handle;
	std::atomic<int>  m_failed_copy_ops;
	void            *m_cpu_addr;
	bool             m_alloced;

	nnpiHostProc::ptr m_proc;
	nnpiWaitQueue    m_waitq;
	int              m_readers; /* 0==unlocked, >0 locked for read, -1 locked for write */
	int              m_cpu_locked; /* locked for cpu access */
	bool             m_cpu_sync_needed;
	uint64_t         m_user_hdl;
};
