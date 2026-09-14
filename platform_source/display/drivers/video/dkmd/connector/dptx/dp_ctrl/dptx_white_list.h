/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
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

#ifndef __DPTX_WHITE_LIST_H__
#define __DPTX_WHITE_LIST_H__
#include "dp_ctrl.h"

// During training, initialize pre and swing based on the whitelist.
void dptx_update_pre_swing_with_white_list(struct dp_ctrl *dptx);
#endif