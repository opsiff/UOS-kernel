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
#ifndef DPU_COMP_TUNNEL_PRESENT_CTL_H
#define DPU_COMP_TUNNEL_PRESENT_CTL_H

#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/semaphore.h>
#include "dkmd_dpu.h"

#ifdef CONFIG_DKMD_DPU_TUNNEL_DEVICE_PRESENT
struct dpu_composer;
struct comp_online_present;
struct dpu_tunnel_ctrl {
	struct dpu_composer *dpu_comp;
	struct comp_online_present *present;
	wait_queue_head_t configurable_tag_wq;
	atomic_t frame_configurable_tag;
	atomic_t dev_commit_layer_cnt;
	atomic_t tunnel_exit_frame;
};

void dpu_tunnel_comp_register(struct dpu_composer *dpu_comp, struct comp_online_present *present);
void dpu_tunnel_comp_unregister(struct dpu_composer *dpu_comp, struct comp_online_present *present);
void dpu_tunnel_device_online_overlay(struct dpu_composer *dpu_comp, struct disp_frame *frame);
void dpu_tunnel_register_vsync_listener(struct dpu_composer *dpu_comp, struct comp_online_present *present);
void dpu_tunnel_unregister_vsync_listener(struct dpu_composer *dpu_comp);
int32_t dpu_tunnel_is_disable_intra_dvfs(struct tunnel_info *tunnel_info);
#else
#define dpu_tunnel_comp_register(dpu_comp, present)
#define dpu_tunnel_comp_unregister(dpu_comp, present)
#define dpu_tunnel_device_online_overlay(dpu_comp, frame)
#define dpu_tunnel_register_vsync_listener(dpu_comp, present)
#define dpu_tunnel_unregister_vsync_listener(dpu_comp)
#define dpu_tunnel_is_disable_intra_dvfs(tunnel_info) (0)
#endif
#endif
