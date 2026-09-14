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
#include <linux/types.h>
#include "dkmd_log.h"
#include "dpu_comp_dfr.h"
#include "dpu_comp_multi_present.h"
#include "dpu_comp_mgr.h"
#include "config/dpu_comp_multi_present_config_utils.h"

static void dpu_multi_present_clear_sfu2vactive_param(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	char __iomem *dpu_base = dfr_ctrl->dpu_comp->comp_mgr->dpu_base;
	dpu_pr_debug("+");
	dpu_dacc_write_sfu2vactive_period(dpu_base, 0);
	dpu_pr_debug("-");
	return;
}

static bool dpu_multi_present_is_safe_period(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	char __iomem *dpu_base = NULL;
	struct comp_online_present *present = NULL;
	ktime_t base_timestamp = 0;
	uint32_t is_sfu2vactive_period = 0;
	uint32_t capture_time = 0;
	bool is_safe_period = true;
	bool has_tunnel_layer = false;
	dpu_check_and_return(!dfr_ctrl->ops || !dfr_ctrl->ops->is_fisrt_frame, false, err, "is_fisrt_frame is null");
	if (unlikely(dfr_ctrl->ops->is_fisrt_frame(dfr_ctrl))) {
		dpu_pr_info("first frame not check safe period");
		return true;
	}

	dpu_base = dfr_ctrl->dpu_comp->comp_mgr->dpu_base;
	is_sfu2vactive_period = dpu_dacc_read_sfu2vactive_period(dpu_base);
	if (is_sfu2vactive_period != 0) {
		dpu_pr_debug("wait vactive start, is_sfu2vactive_period = %u", is_sfu2vactive_period);
		return false;
	}

	present = (struct comp_online_present *)dfr_ctrl->dpu_comp->present_data;
	has_tunnel_layer = present->frames[present->displaying_idx].in_frame.tunnel_info.has_tunnel_layer ||
		present->frames[present->incoming_idx].in_frame.tunnel_info.has_tunnel_layer;

	if (has_tunnel_layer || g_debug_safe_time_period != 0) {
		base_timestamp = present->vactive_start_timestamp > present->vsync_ctrl.timestamp ?
			present->vactive_start_timestamp : present->vsync_ctrl.timestamp;
		capture_time = dpu_dacc_get_capture_time(dpu_base);
		is_safe_period = capture_time == 0 ? true :
			(ktime_us_delta(ktime_get(), base_timestamp) < capture_time - dfr_ctrl->unsafe_period);
	}

	return is_safe_period;
}

static void dpu_multi_present_set_present_config_period(struct dpu_comp_dfr_ctrl *dfr_ctrl, bool flag)
{
	char __iomem *dpu_base = dfr_ctrl->dpu_comp->comp_mgr->dpu_base;
	dpu_dacc_write_present_config_period(dpu_base, (uint32_t)flag);
}

bool dpu_multi_present_in_sfu2vactive_period(struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t client_type)
{
	char __iomem *dpu_base = dfr_ctrl->dpu_comp->comp_mgr->dpu_base;
	bool ret = false;

	if (client_type < PRESENT_CLIENT_TYPE_MAX)
		ret = (dpu_dacc_read_sfu2vactive_period(dpu_base) & BIT(client_type)) != 0;
	else
		dpu_pr_err("[multi_present] client_type is invalid, client_type = %u", client_type);

	return ret;
}

void dpu_multi_present_wake_up_vactive_wait_event(struct comp_online_present *present)
{
	if (present->vactive_start_flag != 0)
		wake_up_interruptible_all(&present->vactive_start_wq);
}

void dpu_multi_present_dump_info(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	char __iomem *dpu_base = dfr_ctrl->dpu_comp->comp_mgr->dpu_base;
	uint32_t sfu2vactive_period_value = dpu_dacc_read_sfu2vactive_period(dpu_base);
	uint32_t capture_time = dpu_dacc_get_capture_time(dpu_base);
	uint32_t present_config_period = dpu_dacc_read_present_config_period(dpu_base);
	uint32_t need_wait_te_num = dpu_dacc_read_need_wait_te_num(dpu_base);
	dpu_pr_info("sfu2vactive_period = %#x, capture_time = %u, present_config_period = %#x need_wait_te_num = %u",
		sfu2vactive_period_value, capture_time, present_config_period, need_wait_te_num);
}

static struct multi_present_ops multi_present_func = {
    .clear_sfu2vactive_param = dpu_multi_present_clear_sfu2vactive_param,
    .is_safe_period = dpu_multi_present_is_safe_period,
	.set_present_config_period = dpu_multi_present_set_present_config_period,
	.in_sfu2vactive_period = dpu_multi_present_in_sfu2vactive_period,
	.wake_up_vactive_wait_event = dpu_multi_present_wake_up_vactive_wait_event,
	.dump_info = dpu_multi_present_dump_info,
};

void dpu_multi_present_register_ops_dfr_by_mcu(struct dpu_multi_present_ctrl *multi_present_ctrl)
{
    multi_present_ctrl->ops = &multi_present_func;
}