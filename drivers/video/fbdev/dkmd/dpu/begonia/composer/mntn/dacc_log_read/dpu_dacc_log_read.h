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
#ifndef DPU_DACC_LOG_READ_H
#define DPU_DACC_LOG_READ_H

#include <linux/types.h>
#include "dpu_comp_mgr.h"
#ifdef CONFIG_MDFX_KMD
void dpu_dacc_log_read_init(char __iomem *dpu_base);
void dpu_dacc_log_read_deinit(void);
uint32_t dpu_exception_dacc_log_dump(void);
void dpu_dacc_log_thread_setup(struct composer_manager *comp_mgr);
void dpu_dacc_log_thread_release(struct composer_manager *comp_mgr);
void dpu_mdp_dacc_log_isr_handler(struct composer_manager *comp_mgr);
void dpu_dacc_log_trans_frm_idx(char __iomem *dpu_base, uint32_t frame_index, uint32_t scene_id);
#else
#define dpu_dacc_log_read_init(dpu_base)
#define dpu_dacc_log_read_deinit()
#define dpu_exception_dacc_log_dump()
#define dpu_dacc_log_thread_setup(comp_mgr)
#define dpu_dacc_log_thread_release(comp_mgr)
#define dpu_mdp_dacc_log_isr_handler(comp_mgr)
#define dpu_dacc_log_trans_frm_idx(dpu_base, frame_index, scene_id)
#endif

#endif