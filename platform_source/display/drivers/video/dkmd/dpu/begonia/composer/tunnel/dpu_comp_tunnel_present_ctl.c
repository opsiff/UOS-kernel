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

#include <linux/delay.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/hwspinlock.h>
#include <dpu/soc_dpu_define.h>
#include "dpu/dpu_base_addr.h"
#include "dpu_comp_tunnel_present_ctl.h"
#include "dpu_config_utils.h"
#include "dkmd_log.h"
#include "dpu_conn_mgr.h"
#include "dpu_comp_mgr.h"
#include "dpu_connector.h"
#include "dpu_comp_vactive.h"

#define TIME_PERIOD_TO_NEXT_VSYNC 4000 // us
#define SECOND_IN_US 1000000
#define TUNNEL_WAIT_TIMEOUT_MS 300
#define VALID_VSYNC_THRESHOLD 20000 // us
/* configurable tag: one vsync period only allow one frame persent config */
static void dpu_tunnel_set_frame_configurable_tag(struct dpu_composer *dpu_comp, int32_t value)
{
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);
	dpu_pr_debug("set value %d", value);
	atomic_set(&tunnel_ctrl->frame_configurable_tag, value);
}

static int32_t dpu_tunnel_frame_configurable_tag_wait(struct dpu_composer *dpu_comp)
{
	int32_t ret = 0;
	uint32_t wait_cnt = 0;
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);

	while (true) {
		dpu_pr_debug("tunnel_timing wait cnt %u", wait_cnt);
		ret = wait_event_interruptible_timeout(tunnel_ctrl->configurable_tag_wq,
			(atomic_read(&tunnel_ctrl->frame_configurable_tag) == 1),
			(long)msecs_to_jiffies(TUNNEL_WAIT_TIMEOUT_MS));
		if ((ret == -ERESTARTSYS) && (wait_cnt++ < 50))
			mdelay(10);
		else
			break;
	}
	return ret;
}

static int32_t dpu_tunnel_timing_ctrl(struct dpu_composer *dpu_comp)
{
	uint64_t cur_ts;
	uint64_t prev_vsync_ts;
	uint64_t vsync_interval;
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);
	uint32_t current_fps = present->dfr_ctrl.cur_frm_rate;
	dpu_check_and_return((current_fps == 0), -1, err, "current_fps is 0");

	dpu_pr_debug("tunnel_timing frame rate %u", current_fps);
	if (atomic_read(&tunnel_ctrl->frame_configurable_tag) == 0) {
		/* wait for vsync coming */
		goto next_vsync_wait;
	}

	vsync_interval = SECOND_IN_US / (uint64_t)current_fps;
	cur_ts = (uint64_t)ktime_to_us(ktime_get());
	prev_vsync_ts = (uint64_t)ktime_to_us(present->vsync_ctrl.timestamp);
	dpu_pr_debug("tunnel_timing cur ts: %llu, prev ts: %llu, vsync interval: %llu",
		cur_ts, prev_vsync_ts, vsync_interval);
	if (vsync_interval < TIME_PERIOD_TO_NEXT_VSYNC || cur_ts <= prev_vsync_ts ||
		(cur_ts - prev_vsync_ts) > vsync_interval) {
		dpu_pr_debug("tunnel_timing cur ts: %llu, prev ts: %llu, vsync interval: %llu",
			cur_ts, prev_vsync_ts, vsync_interval);
		dpu_tunnel_set_frame_configurable_tag(dpu_comp, 0);
		goto next_vsync_wait;
	}
	/* secure period time */
	if ((cur_ts - prev_vsync_ts) < (vsync_interval - TIME_PERIOD_TO_NEXT_VSYNC)) {
		dpu_pr_debug("tunnel_timing secure period time");
		return 0;
	}
	/* blanking interval, clear cfg tag, need wait for next vsync coming */
	dpu_tunnel_set_frame_configurable_tag(dpu_comp, 0);
next_vsync_wait:
	/* wait for vsync coming */
	if (dpu_tunnel_frame_configurable_tag_wait(dpu_comp) <= 0) {
		dpu_pr_err("tunnel_timing wait next vsync timeout");
		return -1;
	}
	/* ensure present after vactive isr */
	udelay(500); /* 500 us */
	dpu_pr_debug("tunnel_timing wait for next vsync success");
	return 0;
}

static void dpu_tunnel_present_update_param(struct dpu_composer *dpu_comp, struct disp_frame *frame)
{
	uint32_t mask;
	char __iomem *dpu_base = NULL;
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);
	int32_t last_layer_cnt = atomic_read(&tunnel_ctrl->dev_commit_layer_cnt);

	atomic_set(&tunnel_ctrl->dev_commit_layer_cnt, frame->tunnel_info.dev_commit_layer_cnt);
	atomic_set(&tunnel_ctrl->tunnel_exit_frame, 0);
	/* dev tunnel layer destroy, close int mask */
	if (last_layer_cnt != 0 && frame->tunnel_info.dev_commit_layer_cnt == 0) {
		atomic_set(&tunnel_ctrl->tunnel_exit_frame, 1);
		dpu_base = dpu_comp->comp_mgr->dpu_base;
		/* close sensorhub mdp dacc ns int mask */
		mask = inp32(DPU_GLB_NS_MDP_TO_IOMCU_MSK_ADDR(dpu_base + DPU_GLB0_OFFSET));
		mask |= DPU_DACC_NS_INT;
		outp32(DPU_GLB_NS_MDP_TO_IOMCU_MSK_ADDR(dpu_base + DPU_GLB0_OFFSET), mask);
		dpu_pr_info("mdp to iomcu first int mask = 0x%x", mask);
	}
}

void dpu_tunnel_device_online_overlay(struct dpu_composer *dpu_comp, struct disp_frame *frame)
{
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);

	dpu_tunnel_present_update_param(dpu_comp, frame);

	/* multi_present_enable use multi_present_timming feature */
	if (dpu_comp->multi_present_ctrl.ops != NULL)
		return;

	/* tunnel timing control scene: 1) dev layer frame 2) dev layer 1->0 frame */
	if (frame->tunnel_info.dev_commit_layer_cnt != 0 || (atomic_read(&tunnel_ctrl->tunnel_exit_frame) != 0)) {
		if (dpu_tunnel_timing_ctrl(dpu_comp) != 0)
			dpu_pr_warn("tunnel scene_id=%d timing ctrl failed!", frame->scene_id);
		present->vactive_start_flag = 0;
		dpu_pr_debug("tunnel_timing clear vactive_start_flag 0");
	}
	/* clear frame configurable tag */
	dpu_tunnel_set_frame_configurable_tag(dpu_comp, 0);
}

static bool dpu_comp_has_low_power_tunnel_layer(struct dpu_composer *dpu_comp)
{
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);
	int32_t layer_cnt = atomic_read(&tunnel_ctrl->dev_commit_layer_cnt);
	dpu_pr_debug("tunnel cnt %d", layer_cnt);
	return (layer_cnt != 0);
}

void dpu_tunnel_comp_register(struct dpu_composer *dpu_comp, struct comp_online_present *present)
{
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);
	tunnel_ctrl->dpu_comp = dpu_comp;
	tunnel_ctrl->present = present;
	init_waitqueue_head(&tunnel_ctrl->configurable_tag_wq);
	atomic_set(&tunnel_ctrl->frame_configurable_tag, 0);
	atomic_set(&tunnel_ctrl->dev_commit_layer_cnt, 0);
	atomic_set(&tunnel_ctrl->tunnel_exit_frame, 0);
	dpu_pr_debug("inited");
}

void dpu_tunnel_comp_unregister(struct dpu_composer *dpu_comp, struct comp_online_present *present)
{
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);
	void_unused(dpu_comp);
	tunnel_ctrl->dpu_comp = NULL;
	tunnel_ctrl->present = NULL;
	atomic_set(&tunnel_ctrl->dev_commit_layer_cnt, 0);
	atomic_set(&tunnel_ctrl->frame_configurable_tag, 0);
	atomic_set(&tunnel_ctrl->tunnel_exit_frame, 0);
	dpu_pr_debug("deinited");
}

static void dpu_tunnel_proc_vsync(struct dpu_composer *dpu_comp, struct ukmd_listener_data *data)
{
	uint32_t mask;
	uint64_t vsync_interval;
	char __iomem *dpu_base = NULL;
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);

	dpu_tunnel_set_frame_configurable_tag(dpu_comp, 1);
	if (atomic_read(&tunnel_ctrl->tunnel_exit_frame) != 0) {
		atomic_set(&tunnel_ctrl->tunnel_exit_frame, 0);
		wake_up_interruptible_all(&tunnel_ctrl->configurable_tag_wq);
		dpu_pr_debug("tunnel exit frame clear");
		return;
	}
	if (!dpu_comp_has_low_power_tunnel_layer(dpu_comp))
		return;
	vsync_interval = (uint64_t)ktime_us_delta(data->notify_timestamp, present->vsync_ctrl.timestamp);
	/* invalid vsync, caused by cpu scheduling */
	if (vsync_interval > VALID_VSYNC_THRESHOLD && present->dfr_ctrl.cur_frm_rate >= 60) { /* 60fps */
		dpu_tunnel_set_frame_configurable_tag(dpu_comp, 0);
		dpu_pr_warn("invalid vsync, vsync interval %llu, fps %u", vsync_interval, present->dfr_ctrl.cur_frm_rate);
		return;
	}
	wake_up_interruptible_all(&tunnel_ctrl->configurable_tag_wq);
	/* unmask sensorhub dacc ns int */
	dpu_base = dpu_comp->comp_mgr->dpu_base;
	mask = inp32(DPU_GLB_NS_MDP_TO_IOMCU_MSK_ADDR(dpu_base + DPU_GLB0_OFFSET));
	mask &= (~DPU_DACC_NS_INT);
	outp32(DPU_GLB_NS_MDP_TO_IOMCU_MSK_ADDR(dpu_base + DPU_GLB0_OFFSET), mask);
	dpu_pr_debug("notify sensorhub vsync int, vsync interval %llu, fps %u",
		vsync_interval, present->dfr_ctrl.cur_frm_rate);
}

static int32_t dpu_tunnel_vsync_isr_notify(struct notifier_block *self, unsigned long action, void *data)
{
	struct dpu_tunnel_ctrl *tunnel_ctrl = NULL;
	struct dpu_composer *dpu_comp = NULL;
	struct ukmd_listener_data *listener_data = NULL;

	listener_data = (struct ukmd_listener_data *)data;
	dpu_check_and_return(!listener_data, -EINVAL, err, "listener_data is null pointer\n");
	tunnel_ctrl = (struct dpu_tunnel_ctrl *)(listener_data->data);
	dpu_check_and_return(!tunnel_ctrl, -EINVAL, err, "vsync_ctrl is null pointer\n");
	dpu_comp = tunnel_ctrl->dpu_comp;
	dpu_check_and_return(!dpu_comp, -EINVAL, err, "dpu_comp is null pointer\n");

	dpu_pr_debug("vsync isr");
	dpu_tunnel_proc_vsync(dpu_comp, listener_data);
	return 0;
}

static struct notifier_block tunnel_vsync_notifier = {
	.notifier_call = dpu_tunnel_vsync_isr_notify,
};

void dpu_tunnel_register_vsync_listener(struct dpu_composer *dpu_comp, struct comp_online_present *present)
{
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);
	ukmd_isr_register_listener(&dpu_comp->comp_mgr->mdp_isr_ctrl,
		&tunnel_vsync_notifier, NOTIFY_TUNNEL_VSYNC, tunnel_ctrl);
	dpu_pr_debug("-");
}

void dpu_tunnel_unregister_vsync_listener(struct dpu_composer *dpu_comp)
{
	ukmd_isr_unregister_listener(&dpu_comp->comp_mgr->mdp_isr_ctrl,
		&tunnel_vsync_notifier, NOTIFY_TUNNEL_VSYNC);
	dpu_pr_debug("-");
}

int32_t dpu_tunnel_is_disable_intra_dvfs(struct tunnel_info *tunnel_info)
{
	dpu_check_and_return(!tunnel_info, 0, err, "tunnel_info is null pointer\n");
	dpu_pr_debug("device tunnel layer count %u", tunnel_info->dev_commit_layer_cnt);
	if (tunnel_info->dev_commit_layer_cnt != 0)
		return 1;
	return 0;
}