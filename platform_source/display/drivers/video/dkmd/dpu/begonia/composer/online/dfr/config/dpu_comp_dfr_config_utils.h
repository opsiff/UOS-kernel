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

#ifndef DPU_COMP_DFR_CONFIG_UTILS
#define DPU_COMP_DFR_CONFIG_UTILS

#include <linux/types.h>

enum {
	CMDLIST_LDI_BY_MCU_SEND_FRM,
	CMDLIST_LDI_BY_MCU_TYPE_MAX
};

struct cmdlist_config {
	uint32_t active_id;
	uint32_t cmdlist_scene_id;
	uint32_t header_cmdlist_ids[CMDLIST_LDI_BY_MCU_TYPE_MAX];
	uint32_t reg_cmdlist_ids[CMDLIST_LDI_BY_MCU_TYPE_MAX];
};

struct dpu_comp_dfr_ctrl;
struct cmdlist_config;

void dpu_dacc_update_frame_rate_info(char __iomem *dpu_base, uint32_t frm_rate, uint32_t te_rate, uint32_t te_mask_num);
void dpu_dacc_dfr_setup_data(char __iomem *dpu_base, uint32_t lcd_te_idx, uint32_t init_config);
void dpu_dacc_set_dfr_enable(char __iomem *dpu_base, uint32_t flag);
void dpu_dacc_clear_multi_config(char __iomem *dpu_base);
void dpu_dacc_set_need_wait_te_num(char __iomem *dpu_base, uint32_t skip_num);
int32_t dpu_dacc_commit_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct cmdlist_config *cmdlist);
void dpu_dacc_setup_priv_data_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct cmdlist_config *cmdlist);
void dpu_dacc_release_data_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct cmdlist_config *cmdlist);

#endif /* DISP_COMP_CONFIG_UTILS_H */
