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

#ifndef DKMD_SAFE_MARGIN_INFO_H
#define DKMD_SAFE_MARGIN_INFO_H

#include <linux/types.h>

struct safe_margin_info {
	uint32_t vactive2te_safe_margin;
	uint32_t te2te_safe_margin;
};

#endif