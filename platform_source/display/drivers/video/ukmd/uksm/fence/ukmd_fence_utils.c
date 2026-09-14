/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2021. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "ukmd_log.h"

#include <linux/file.h>
#include <linux/sync_file.h>
#include <linux/fs.h>
#ifdef CONFIG_LIBLINUX
#include <liblinux/file_utils.h>
#endif

#include "ukmd_fence_utils.h"

int32_t ukmd_fence_get_fence_fd(struct dma_fence *fence)
{
	int32_t fd;
	struct sync_file *sync_file = NULL;

	if (unlikely(!fence)) {
		ukmd_pr_err("fence is null");
		return -1;
	}

	sync_file = sync_file_create(fence);
	if (!sync_file) {
		ukmd_pr_err("failed to create sync file");
		return -ENOMEM;
	}

#ifdef CONFIG_LIBLINUX
	/**
	 * liblinux_ext_alloc_unused_fd is equivalent to
	 * get_unused_fd_flags and fd_install.
	 */
	fd = liblinux_ext_alloc_unused_fd(O_CLOEXEC, sync_file->file);
	if (fd < 0) {
		fput(sync_file->file); // also free dma_fence mem.
		ukmd_pr_err("fail to get unused fd");
		return fd;
	}
#else
	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0) {
		fput(sync_file->file);
		ukmd_pr_err("fail to get unused fd");
		return fd;
	}

	fd_install((uint32_t)fd, sync_file->file);
#endif

	return fd;
}

int32_t ukmd_fence_signal_fence(int32_t fence_fd)
{
	struct dma_fence *fence = NULL;

	if (fence_fd < 0)
		return 0;

	fence = sync_file_get_fence(fence_fd);
	if (!fence) {
		ukmd_pr_err("fence_fd=%d sync_file_get_fence failed!", fence_fd);
		return -EINVAL;
	}

	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return 0;

	if (dma_fence_signal_timestamp(fence, ktime_get()) != 0)
		ukmd_pr_err("fence_fd %d signal timestamp fail", fence_fd);

	dma_fence_put(fence);

	ukmd_pr_debug("signal fence fd %d", fence_fd);
	return 0;
}

