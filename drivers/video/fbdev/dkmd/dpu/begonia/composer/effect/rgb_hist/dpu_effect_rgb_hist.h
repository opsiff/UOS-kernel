
/* Copyright (c) 2025-2025, Hisilicon Tech. Co., Ltd. All rights reserved.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 and
* only version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
* GNU General Public License for more details.
*
*/

#ifndef DPU_EFFECT_RGB_HIST_H
#define DPU_EFFECT_RGB_HIST_H

#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "dkmd_comp.h"

struct composer;
struct dpu_composer;

struct rgb_hist_status {
	wait_queue_head_t wq_hist;
	bool new_hist;
	bool hist_stop; // fb blank
};

struct dkmd_rgb_hist_ctrl {
	struct semaphore hist_lock_sem;
	struct dkmd_rgb_hist_data hist_data;
    struct mutex hist_data_lock;
	struct rgb_hist_status hist_status;
	char __iomem *dpp_base;
	bool hist_status_inited;
	bool last_frm_rgb_hist_en;

	void (*queue_work)(struct dpu_composer *dpu_comp);
	int32_t (*get_hist)(struct dkmd_rgb_hist_ctrl *ctrl, void __user* argp);
	void (*wake_up_hist)(struct dkmd_rgb_hist_ctrl *ctrl);
};

void dpu_effect_rgb_hist_init(struct dpu_composer *dpu_comp, uint32_t dpp_offset);
void dpu_effect_rgb_hist_deinit(struct dpu_composer *dpu_comp);
#endif