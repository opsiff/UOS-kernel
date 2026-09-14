/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
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

#ifndef COMPOSER_DFR_LTPS_H
#define COMPOSER_DFR_LTPS_H

#include "dpu_comp_dfr.h"

void dfr_ltps_register_ops(struct dpu_comp_dfr_ctrl *dfr_ctrl);
void dfr_ltps_register_ops_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl);

#endif
