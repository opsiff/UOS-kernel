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
#include "dkmd_log.h"
#include "dpu_comp_dfr.h"
#include "dpu_comp_multi_present.h"

void dpu_multi_present_init(struct dpu_multi_present_ctrl *multi_present_ctrl, int32_t dfr_mode)
{
	multi_present_ctrl->ops = NULL;
	switch (dfr_mode) {
	case DFR_MODE_TE_SKIP_BY_ACPU:
		dpu_multi_present_register_ops_dfr_by_acpu(multi_present_ctrl);
		break;
	case DFR_MODE_TE_SKIP_BY_MCU:
	case DFR_MODE_LONGH_TE_SKIP_BY_MCU:
	case DFR_MODE_LONGV_BY_MCU:
	case DFR_MODE_LONGH_BY_MCU:
		dpu_multi_present_register_ops_dfr_by_mcu(multi_present_ctrl);
		break;
	default:
		break;
	}
	return;
}

