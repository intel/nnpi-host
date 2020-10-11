/*******************************************
 * Copyright (C) 2017-2020 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 *******************************************/

#include "nnpiDevice.h"
#include "nnpiHostProc.h"
#include <misc/intel_nnpi.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include "ipc_chan_protocol.h"
#include <misc/nnp_error.h>

nnpiDevice::vec nnpiDevice::s_devices;

#define PCI_BAR_INFO_SIZE 3

NNPError nnpiDevice::errnoToNNPError(uint8_t nnp_kernel_error)
{
	int error = nnp_kernel_error;

	if (error == 0)
		error = errno;

	switch (error) {
	case 0:
		return NNP_NO_ERROR;
	case ENOTTY:
	case EBADF:
	case ENODEV:
		return NNP_NO_SUCH_DEVICE;

	case NNPER_DEVICE_NOT_READY:
		return NNP_DEVICE_NOT_READY;

	case ENOSPC:
	case ENOMEM:
		return NNP_OUT_OF_MEMORY;

	case NNPER_NO_SUCH_RESOURCE:
		return NNP_NO_SUCH_RESOURCE;

	case NNPER_NO_SUCH_CONTEXT:
		return NNP_NO_SUCH_CONTEXT;

	case EBADFD: // fd of dma_buf supplied cannot be used for hostres
	case EPERM: // wrong direction hostres lock/unlock is not permited
	case NNPER_INCOMPATIBLE_RESOURCES:
		return NNP_INCOMPATIBLE_RESOURCES;

	case NNPER_NO_SUCH_NETWORK:
		return NNP_NO_SUCH_NETWORK;

	case NNPER_INCOMPLETE_NETWORK:
		return NNP_INCOMPLETE_NETWORK;

	case EFBIG:
	case NNPER_TOO_MANY_CONTEXTS:
		return NNP_TOO_MANY_CONTEXTS;

	case ENOBUFS:
	case EINVAL:
		return NNP_INVALID_ARGUMENT;

	case NNPER_CONTEXT_BROKEN:
		return NNP_CONTEXT_BROKEN;

	case NNPER_HOSTRES_BROKEN:
		return NNP_HOSTRES_BROKEN;

	case EPIPE:
	case NNPER_DEVICE_ERROR:
		return NNP_DEVICE_ERROR;

	case EBUSY:
	case ETIME:
	case NNPER_TIMED_OUT:
		return NNP_TIMED_OUT;

	case EBADRQC:
	case NNPER_BROKEN_MARKER:
		return NNP_BROKEN_MARKER;

	case EIO:
		return NNP_IO_ERROR;

	case NNPER_NO_SUCH_COPY_HANDLE:
		return NNP_NO_SUCH_COPY_HANDLE;

	case NNPER_NO_SUCH_CMDLIST:
		return NNP_NO_SUCH_CMDLIST;

	case NNPER_NO_SUCH_INFREQ_HANDLE:
		return NNP_NO_SUCH_INFREQ_HANDLE;

	case EFAULT:
	case NNPER_INTERNAL_DRIVER_ERROR:
		return NNP_INTERNAL_DRIVER_ERROR;

	case EINTR:
		return NNP_OPERATION_INTERRUPTED;

	case NNPER_NOT_SUPPORTED:
		return NNP_NOT_SUPPORTED;

	case NNPER_INVALID_EXECUTABLE_NETWORK_BINARY:
		return NNP_INVALID_EXECUTABLE_NETWORK_BINARY;

	case NNPER_INFER_MISSING_RESOURCE:
		return NNP_INFER_MISSING_RESOURCE;

	case NNPER_INSUFFICIENT_RESOURCES:
		return NNP_DEVNET_RESERVE_INSUFFICIENT_RESOURCES;

	case NNPER_ECC_ALLOC_FAILED:
		return NNP_OUT_OF_ECC_MEMORY;

	case NNPER_VERSIONS_MISMATCH:
		return NNP_VERSIONS_MISMATCH;
	}

	return NNP_UNKNOWN_ERROR;
}

int nnpiDevice::findMaxDeviceNumber(void)
{
	int max_devnum = -1;
	DIR *subdirs = opendir("/dev");

	if (subdirs) {
		struct dirent *sent;

		while ((sent = readdir(subdirs)) != NULL) {
			if (strncmp(sent->d_name, "nnpi", 4) == 0) {
				int l = strlen(sent->d_name);

				if (l > 4 && isdigit(sent->d_name[4])) {
					int devnum;

					if (sscanf(&sent->d_name[4], "%d", &devnum) == 1) {
						if (devnum > max_devnum)
							max_devnum = devnum;
					}
				}
			}
		}
		closedir(subdirs);
	}

	return max_devnum;
}

void nnpiDevice::close_devices()
{
	nnpiDevice::ptr dev;

	s_devices.lock();
	for (auto it = s_devices.begin(); it != s_devices.end(); ++it) {
		dev = it->lock();
		if (dev.get() != nullptr) {
			if (dev->m_fd >= 0) {
				close(dev->m_fd);
				dev->m_fd = -1;
			}
		}
	}
	s_devices.unlock();
}

void nnpiDevice::lock_all()
{
	nnpiDevice::ptr dev;

	s_devices.lock();
	for (auto it = s_devices.begin(); it != s_devices.end(); ++it) {
		dev = it->lock();
		if (dev.get() != nullptr)
			dev->m_mutex.lock();
	}
}

void nnpiDevice::unlock_all()
{
	nnpiDevice::ptr dev;

	for (auto it = s_devices.begin(); it != s_devices.end(); ++it) {
		dev = it->lock();
		if (dev.get() != nullptr)
			dev->m_mutex.unlock();
	}
	s_devices.unlock();
}

// clear_devices should be called only when s_devices is empty so no lock protection is needed
void nnpiDevice::clear_devices(bool only_contexts)
{
	nnpiDevice::ptr dev;

	s_devices.lock();
	for (auto it = s_devices.begin(); it != s_devices.end();) {
		dev = it->lock();
		if (dev.get() != nullptr) {
			dev->close_all_chan_fds(only_contexts);
			if (dev->m_chan_fds.empty())
				it = s_devices.erase(it);
			else
				++it;
		}
		else
			++it;
	}
	s_devices.unlock();
}

nnpiDevice::ptr nnpiDevice::get(uint32_t dev_num)
{
	nnpiDevice *ptr;

	if (dev_num + 1 == 0)
		return nullptr;

	s_devices.lock();
	if (s_devices.size() <= dev_num) {
		int max_dev = findMaxDeviceNumber();

		if (max_dev < 0 || dev_num > static_cast<uint32_t>(max_dev)) {
			s_devices.unlock();
			return nullptr;
		}
		s_devices.resize(dev_num + 1);
	}
	nnpiDevice::ptr device_ptr = s_devices[dev_num].lock();
	if (device_ptr.get() == nullptr) {
		char dev_filename[32];
		snprintf(dev_filename, sizeof(dev_filename),
			 "/dev/" NNPI_DEVICE_DEV_FMT, dev_num);
		int fd = open(dev_filename, O_RDWR | O_CLOEXEC);
		if (fd >= 0) {
			ptr = new nnpiDevice(dev_num, fd);
			if (ptr == nullptr) {
				s_devices.unlock();
				close(fd);
				return device_ptr;
			}

			if (ptr->getBARAddr()) {
				delete ptr;
				s_devices.unlock();
				return device_ptr;
			}
			device_ptr.reset(ptr);
			s_devices[dev_num] = device_ptr;
		}

	}
	s_devices.unlock();
	return device_ptr;
}

int nnpiDevice::createChannel(const nnpiHostProc::ptr &host,
			      uint32_t                 weight,
			      bool                     is_context,
			      bool                     get_device_events,
			      uint16_t                *out_id,
			      int                     *out_fd,
			      int                     *out_privileged)
{
	struct ioctl_nnpi_create_channel req;
	int ret;

	if (!out_id || !out_fd)
		return EINVAL;

	if (host->fd() < 0 || m_fd < 0)
		return ENODEV;

	std::unique_lock<std::mutex> lock(m_mutex);
	do {
		memset(&req, 0, sizeof(req));
		req.i_weight = weight;
		req.i_host_fd = host->fd();
		req.i_min_id = (is_context ? 0 : 256);
		req.i_max_id = (is_context ? 255 : (1 << NNP_IPC_CHANNEL_BITS) - 1);
		req.i_get_device_events = get_device_events;
		req.i_protocol_version = NNP_IPC_CHAN_PROTOCOL_VERSION;

		ret = ioctl(m_fd, IOCTL_NNPI_DEVICE_CREATE_CHANNEL, &req);
	} while (ret < 0 && errno == EINTR);

	if (ret != 0)
		return errno;
	else if (req.o_errno != 0)
		return req.o_errno;

	m_chan_fds.insert(std::pair<int,bool>(req.o_fd,is_context));

	lock.unlock();

	*out_id = req.o_channel_id;
	*out_fd = req.o_fd;
	*out_privileged = req.o_privileged;

	return 0;
}

void nnpiDevice::close_all_chan_fds(bool only_contexts)
{
	m_mutex.lock();
	for (auto fd_map = m_chan_fds.begin(); fd_map != m_chan_fds.end();) {
		if (only_contexts == false || fd_map->second) {
			close(fd_map->first);
			fd_map = m_chan_fds.erase(fd_map);
		}
		else
			++fd_map;
	}
	m_mutex.unlock();
}

void nnpiDevice::closeChannel(int fd)
{
	m_mutex.lock();
	close(fd);
	m_chan_fds.erase(fd);
	m_mutex.unlock();
}

int nnpiDevice::createChannelRingBuffer(uint16_t  channel_id,
					uint8_t   rb_id,
					bool      is_h2c,
					nnpiHostRes::ptr hostres)
{
	struct ioctl_nnpi_create_channel_data_ringbuf req;
	int ret;

	if (hostres.get() == nullptr)
		return EINVAL;

	if (m_fd < 0)
		return ENODEV;

	do {
		memset(&req, 0, sizeof(req));
		req.i_channel_id = channel_id;
		req.i_id = rb_id;
		req.i_h2c = is_h2c;
		req.i_hostres_handle = hostres->kmd_handle();
		req.o_errno = 0;

		ret = ioctl(m_fd, IOCTL_NNPI_DEVICE_CREATE_CHANNEL_RB, &req);
	} while (ret < 0 && errno == EINTR);

	if (ret != 0)
		return errno;
	else if (req.o_errno != 0)
		return req.o_errno;

	return 0;
}

int nnpiDevice::destroyChannelRingBuffer(uint16_t  channel_id,
					 uint8_t   rb_id,
					 bool      is_h2c)
{
	struct ioctl_nnpi_destroy_channel_data_ringbuf req;
	int ret;

	if (m_fd < 0)
		return ENODEV;

	do {
		memset(&req, 0, sizeof(req));
		req.i_channel_id = channel_id;
		req.i_id = rb_id;
		req.i_h2c = is_h2c;

		ret = ioctl(m_fd, IOCTL_NNPI_DEVICE_DESTROY_CHANNEL_RB, &req);
	} while (ret < 0 && errno == EINTR);

	if (ret != 0 && req.o_errno == 0)
		return errno;

	return req.o_errno;
}

int nnpiDevice::mapHostResource(uint16_t         channel_id,
				nnpiHostRes::ptr hostres,
				uint16_t        &out_map_id)
{
	struct ioctl_nnpi_channel_map_hostres req;
	int ret;

	if (m_fd < 0)
		return ENODEV;

	do {
		memset(&req, 0, sizeof(req));
		req.i_channel_id = channel_id;
		req.i_hostres_handle = hostres->kmd_handle();
		req.o_errno = 0;

		ret = ioctl(m_fd, IOCTL_NNPI_DEVICE_CHANNEL_MAP_HOSTRES, &req);
	} while (ret < 0 && errno == EINTR);

	if (ret != 0)
		return errno;
	else if (req.o_errno != 0)
		return req.o_errno;

	out_map_id = req.o_map_id;
	if (req.o_sync_needed)
		hostres->enableCPUsync();

	return 0;
}

int nnpiDevice::unmapHostResource(uint16_t  channel_id,
				  uint16_t  map_id)
{
	struct ioctl_nnpi_channel_unmap_hostres req;
	int ret;

	if (m_fd < 0)
		return ENODEV;

	do {
		memset(&req, 0, sizeof(req));
		req.i_channel_id = channel_id;
		req.i_map_id = map_id;
		req.o_errno = 0;

		ret = ioctl(m_fd, IOCTL_NNPI_DEVICE_CHANNEL_UNMAP_HOSTRES, &req);
	} while (ret < 0 && errno == EINTR);

	if (ret != 0)
		return errno;
	else if (req.o_errno != 0)
		return req.o_errno;

	return 0;
}

int nnpiDevice::getBARAddr()
{

#ifdef HW_LAYER_NNP

	char file_name[PATH_MAX];
	FILE *file;
	char line[128];

	snprintf(file_name, PATH_MAX,  "/sys/class/nnpi/nnpi%u/device/resource", m_dev_num);
	file = fopen(file_name, "rt");
	if (!file)
		return -1;

	/* Read BAR0*/
	if (fgets(line, sizeof(line), file) == NULL)
		return -1;
	if (sscanf(line, "0x%lX", &bar0_addr) != 1)
		return -1;

	/* Skip the line (BAR1) */
	if (fgets(line, sizeof(line), file) == NULL)
		return -1;

	/* Read BAR2*/
	if (fgets(line, sizeof(line), file) == NULL)
		return -1;
	if (sscanf(line, "0x%lX", &bar2_addr) != 1)
		return -1;

	fclose(file);
#endif
	return 0;
}
