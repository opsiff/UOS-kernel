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

#ifndef DPU_COMP_MULTI_PRESENT_CONFIG_UTILS
#define DPU_COMP_MULTI_PRESENT_CONFIG_UTILS

#include <linux/types.h>
uint32_t dpu_dacc_read_sfu2vactive_period(char __iomem *dpu_base);
uint32_t dpu_dacc_get_capture_time(char __iomem *dpu_base);
void dpu_dacc_write_sfu2vactive_period(char __iomem *dpu_base, uint32_t flag);
void dpu_dacc_write_present_config_period(char __iomem *dpu_base, uint32_t flag);
uint32_t dpu_dacc_read_present_config_period(char __iomem *dpu_base);
uint32_t dpu_dacc_read_need_wait_te_num(char __iomem *dpu_base);
#endif /* DISP_COMP_CONFIG_UTILS_H */
