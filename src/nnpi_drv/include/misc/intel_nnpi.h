/* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */

/********************************************
 * Copyright (C) 2019-2020 Intel Corporation
 ********************************************/
#ifndef _NNP_UAPI_H
#define _NNP_UAPI_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <stdbool.h>
#ifndef __KERNEL__
#include <stdint.h>
#else
#include <linux/types.h>
#endif

#define NNPDRV_INF_HOST_DEV_NAME "nnpi_host"

/*
 * ioctls for /dev/nnpi_host device
 */

/*
 * IOCTL_INF_CREATE_HOST_RESOURCE:
 *
 * A request to create a host memory resource object that can then be mapped
 * and accessed by the NNP-I device's DMA engine.
 * The created host resource is pinned in memory for its entire lifecycle.
 * Depending on the argument of the IOCTL, the memory of the resource can be
 * allocated by the IOCTL call, it can be backed by user allocated memory which
 * get pinned by the IOCTL or it can be backed by dma-buf object created by
 * another driver.
 *
 * See describtion of nnpdrv_ioctl_create_hostres structure for more details.
 *
 * The ioctl returns a handle to the created host resource, this returned handle
 * can also be used in the offset argument of mmap(2) for mapping the resource
 * to the application address space.
 */
#define IOCTL_INF_CREATE_HOST_RESOURCE      \
	_IOWR('h', 0, struct nnpdrv_ioctl_create_hostres)

/*
 * IOCTL_INF_DESTROY_HOST_RESOURCE:
 *
 * A request to destoy a host resource object.
 */
#define IOCTL_INF_DESTROY_HOST_RESOURCE     \
	_IOWR('h', 2, struct nnpdrv_ioctl_destroy_hostres)

/*
 * IOCTL_INF_LOCK_HOST_RESOURCE:
 *
 * A request to lock a host resource for cpu access for either
 * read or write.
 *
 * This IOCTL does *not* synchronize accessed to host memory between host
 * cpu and the device's DMA engine. It is used only for either flush or
 * invalidate cpu caches to let the device see the last writes made from
 * host cpu and let cpu read up-to-date content of the resource after the
 * device changed it.
 *
 * This synchronization is not required on all platforms, when mapping
 * the resource for device access, using IOCTL_NNPI_DEVICE_CHANNEL_MAP_HOSTRES,
 * the application receive an indication if such synchronization is needed
 * or not with that device.
 *
 * When such synchronization is needed:
 * When application wants to change host resource content to be read by the
 * device, it should first lock it for write, change its content by accessing
 * it's mmaped virtual address and then call this ioctl again to unlock it
 * before sending a command to the device which may read the resource.
 * When the application received indication that the device has changed the
 * resource content, it should first lock the resource for reading before
 * accessing its memory.
 */
#define IOCTL_INF_LOCK_HOST_RESOURCE        \
	_IOWR('h', 3, struct nnpdrv_ioctl_lock_hostres)

/*
 * IOCTL_INF_UNLOCK_HOST_RESOURCE:
 *
 * A request to unlock a host resource that was previously locked for cpu access.
 */
#define IOCTL_INF_UNLOCK_HOST_RESOURCE      \
	_IOWR('h', 4, struct nnpdrv_ioctl_lock_hostres)

/*
 * The below are possible bit masks that can be specified in
 * usage_flags field of struct nnpdrv_ioctl_create_hostres.
 * It specify attribute and usage flags for a host resource.
 */
#define IOCTL_INF_RES_INPUT          BIT(0) /* being read by the NNP-I device */
#define IOCTL_INF_RES_OUTPUT         BIT(1) /* being written by the device */

/**
 * struct nnpdrv_ioctl_create_hostres - IOCTL_INF_CREATE_HOST_RESOURCE payload
 * @size: When set to zero on input, indicate to create a host resource
 *        which is attached to the dma-buf fd provided in @dma_buf field,
 *        On output it will include the host resource size.
 *        When set to non-zero value, the dma_buf field will be ignored and
 *        it specified the size in bytes of the memory to either allocate
 *        or pin.
 * @dma_buf: fd of dma-buf to attach to. Ignored if @size is not 0.
 * @usage_flags: resource usage flag bits, IOCTL_INF_RES_*
 * @user_handle: On input, if set to 0, indicate that memory for the resource
 *               needs to be allocated, otherwise it specified user virtual
 *               address that will be pinned by that resource.
 *               On output, it includes a handle to the host resource object
 *               that can be used with other IOCTLs later and for mapping to
 *               application user space.
 *
 * argument structure for IOCTL_INF_CREATE_HOST_RESOURCE ioctl
 */
struct nnpdrv_ioctl_create_hostres {
	__u64 size;
	__u32 dma_buf;
	__u32 usage_flags;
	__u64 user_handle;
};

/**
 * struct nnpdrv_ioctl_lock_hostres - IOCTL_INF_LOCK_HOST_RESOURCE payload
 * @user_handle: handle to host resource object
 * @o_errno: On output, 0 on success, one of the NNPERR_* error codes on error.
 *
 * argument structure for IOCTL_INF_LOCK_HOST_RESOURCE and
 * IOCTL_INF_LOCK_HOST_RESOURCE ioctl calls.
 */
struct nnpdrv_ioctl_lock_hostres {
	__u64 user_handle;
	__u8  o_errno;
};

/**
 * struct nnpdrv_ioctl_destroy_hostres - IOCTL_INF_DESTROY_HOST_RESOURCE payload
 * @user_handle: handle to host resource object
 * @o_errno: On output, 0 on success, one of the NNPERR_* error codes on error.
 *
 * argument structure for IOCTL_INF_DESTROY_HOST_RESOURCE ioctl
 */
struct nnpdrv_ioctl_destroy_hostres {
	__u64 user_handle;
	__u8  o_errno;
};

/*
 * ioctls for /dev/nnpi%d device
 */
#define NNPI_DEVICE_DEV_FMT "nnpi%u"

/**
 * IOCTL_NNPI_DEVICE_CREATE_CHANNEL:
 *
 * A request to create a new communication "channel" with an NNP-I device.
 * This channel can be used to send command and receive responses from the
 * device.
 */
#define IOCTL_NNPI_DEVICE_CREATE_CHANNEL      \
	_IOWR('D', 0, struct ioctl_nnpi_create_channel)

/**
 * IOCTL_NNPI_DEVICE_CREATE_CHANNEL_RB:
 *
 * A request to create a data ring buffer for a command channel object.
 * This is used to transfer data together with command to the device.
 * A device command may include a data size fields which indicate how much data
 * has pushed into that ring-buffer object.
 */
#define IOCTL_NNPI_DEVICE_CREATE_CHANNEL_RB   \
	_IOWR('D', 1, struct ioctl_nnpi_create_channel_data_ringbuf)

/**
 * IOCTL_NNPI_DEVICE_DESTROY_CHANNEL_RB:
 *
 * A request to destoy a data ring buffer allocated for a command channel.
 */
#define IOCTL_NNPI_DEVICE_DESTROY_CHANNEL_RB  \
	_IOWR('D', 2, struct ioctl_nnpi_destroy_channel_data_ringbuf)

/**
 * IOCTL_NNPI_DEVICE_CHANNEL_MAP_HOSTRES:
 *
 * A request to map a host resource to a command channel object.
 * Device commands can include "map id" of this mapping for referencing
 * a host resource.
 */
#define IOCTL_NNPI_DEVICE_CHANNEL_MAP_HOSTRES \
	_IOWR('D', 3, struct ioctl_nnpi_channel_map_hostres)

/**
 * IOCTL_NNPI_DEVICE_CHANNEL_UNMAP_HOSTRES:
 *
 * A request to unmap a host resource previously mapped to a command channel.
 */
#define IOCTL_NNPI_DEVICE_CHANNEL_UNMAP_HOSTRES \
	_IOWR('D', 4, struct ioctl_nnpi_channel_unmap_hostres)

/**
 * struct ioctl_nnpi_create_channel - IOCTL_NNPI_DEVICE_CREATE_CHANNEL payload
 * @i_weight: controls how much command submission bandwidth this channel will
 *            get comparing to other channels. This value defines how much
 *            commands should be transmitted (if available) to the device from
 *            this channel before scheduling transmision from other channels.
 * @i_host_fd: opened file descriptor to /dev/nnpi_host
 * @i_min_id: minimum range for channel id allocation
 * @i_max_id: maximum range for channel id allocation
 * @i_get_device_events: if true, device-level event responses will be
 *            delivered to be read from the channel.
 * @i_protocol_version: The NNP_IPC_CHAN_PROTOCOL_VERSION the user-space has
 *                      compiled with.
 * @o_fd: returns file-descriptor through which commands/responses can be
 *        write/read.
 * @o_channel_id: returns the unique id of the channel
 * @o_privileged: true if the channel is priviledged
 * @o_errno: On output, 0 on success, one of the NNPERR_* error codes on error.
 */
struct ioctl_nnpi_create_channel {
	__u32    i_weight;
	__s32    i_host_fd;
	__s32    i_min_id;
	__s32    i_max_id;
	__s32    i_get_device_events;
	__u16    i_protocol_version;
	__s32    o_fd;
	__u16    o_channel_id;
	__s32    o_privileged;
	__u8     o_errno;
};

/**
 * struct ioctl_nnpi_create_channel_data_ringbuf
 * @i_channel_id: command channel id.
 * @i_id: id of the ring buffer object (can be 0 or 1).
 * @i_h2c: non-zero if this ring-buffer is for command submission use,
 *         otherwise it is for responses.
 * @i_hostres_handle: handle of a host resource which will be used to hold
 *         the ring-buffer content.
 * @o_errno: On output, 0 on success, one of the NNPERR_* error codes on error.
 *
 * this is the payload for IOCTL_NNPI_DEVICE_CREATE_CHANNEL_RB ioctl
 */
struct ioctl_nnpi_create_channel_data_ringbuf {
	__u16 i_channel_id;
	__u8  i_id;
	__u8  i_h2c;
	__u64 i_hostres_handle;
	__u8  o_errno;
};

/**
 * struct ioctl_nnpi_destroy_channel_data_ringbuf
 * @i_channel_id: command channel id.
 * @i_id: id of the ring buffer object (can be 0 or 1).
 * @i_h2c: true if this ring-buffer is for command submission use,
 *         otherwise it is for responses.
 * @o_errno: On output, 0 on success, one of the NNPERR_* error codes on error.
 *
 * this is the payload for IOCTL_NNPI_DEVICE_DESTROY_CHANNEL_RB ioctl
 */
struct ioctl_nnpi_destroy_channel_data_ringbuf {
	__u16 i_channel_id;
	__u8  i_id;
	__u8  i_h2c;
	__u8  o_errno;
};

/**
 * struct ioctl_nnpi_channel_map_hostres
 * @i_channel_id: command channel id.
 * @i_hostres_handle: handle of a host resource to be mapped
 * @o_map_id: returns unique id of the mapping
 * @o_sync_needed: returns non-zero if LOCK/UNLOCK_HOST_RESOURCE ioctls
 *            needs to be used before/after accessing the resource from cpu.
 * @o_errno: On output, 0 on success, one of the NNPERR_* error codes on error.
 *
 * this is the payload for IOCTL_NNPI_DEVICE_CHANNEL_MAP_HOSTRES ioctl
 */
struct ioctl_nnpi_channel_map_hostres {
	__u16 i_channel_id;
	__u64 i_hostres_handle;
	__u16 o_map_id;
	__u8  o_sync_needed;
	__u8  o_errno;
};

/**
 * ioctl_nnpi_channel_unmap_hostres
 * @i_channel_id: command channel id.
 * @i_map_id: mapping id
 * @o_errno: On output, 0 on success, one of the NNPERR_* error codes on error.
 *
 * This is the payload for IOCTL_NNPI_DEVICE_CHANNEL_UNMAP_HOSTRES ioctl
 */
struct ioctl_nnpi_channel_unmap_hostres {
	__u16 i_channel_id;
	__u16 i_map_id;
	__u8  o_errno;
};

/****************************************************************
 * Error code values - errors returned in o_errno fields of
 * above structures.
 ****************************************************************/
#define	NNP_ERRNO_BASE	                        200
#define	NNPER_DEVICE_NOT_READY			(NNP_ERRNO_BASE + 1)
#define	NNPER_NO_SUCH_RESOURCE			(NNP_ERRNO_BASE + 2)
#define	NNPER_INCOMPATIBLE_RESOURCES		(NNP_ERRNO_BASE + 3)
#define	NNPER_DEVICE_ERROR			(NNP_ERRNO_BASE + 4)
#define NNPER_NO_SUCH_CHANNEL                   (NNP_ERRNO_BASE + 5)
#define NNPER_NO_SUCH_HOSTRES_MAP               (NNP_ERRNO_BASE + 6)
#define NNPER_VERSIONS_MISMATCH                 (NNP_ERRNO_BASE + 7)

#endif /* of _NNP_UAPI_H */
