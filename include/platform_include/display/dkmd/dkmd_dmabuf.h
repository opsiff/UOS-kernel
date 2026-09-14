/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
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

#ifndef DKMD_DMABUF_H
#define DKMD_DMABUF_H

#include <linux/types.h>

int32_t dpu_dev_map_dmabuf_by_sharefd(int32_t fd, uint32_t size, uint64_t *iova);
int32_t dpu_dev_unmap_dmabuf_by_sharefd(int32_t fd, uint64_t iova);
#endif