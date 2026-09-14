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
#include "ukmd_listener.h"
#include "dpu_comp_mgr.h"
#include "dpu_isr.h"
#include "effect/rgb_hist/dpu_effect_rgb_hist.h"

static int32_t dpu_comp_rgb_hist_handle_isr_notify(struct notifier_block *self, unsigned long action, void *data)
{
	struct ukmd_listener_data *listener_data = (struct ukmd_listener_data *)data;
	struct dpu_composer *dpu_comp = (struct dpu_composer *)(listener_data->data);
	dpu_check_and_return(!dpu_comp, -1, err, "dpu_comp is null pointer");

	if (dpu_comp->rgb_hist_ctrl) {
		uint32_t rgb_hist = inp32(DPU_DPP_RGB_HIST_EN_ADDR(dpu_comp->rgb_hist_ctrl->dpp_base));
		if (dpu_comp->rgb_hist_ctrl->last_frm_rgb_hist_en)
			dpu_comp->rgb_hist_ctrl->queue_work(dpu_comp);

		dpu_comp->rgb_hist_ctrl->last_frm_rgb_hist_en = (rgb_hist & 0x1) != 0;
	}

	return 0;
}

static struct notifier_block rgb_hist_handle_isr_notifier = {
	.notifier_call = dpu_comp_rgb_hist_handle_isr_notify,
};

void dpu_comp_rgb_hist_handle_init(struct ukmd_isr *isr_ctrl, struct dpu_composer *dpu_comp, uint32_t listening_bit)
{
	ukmd_isr_register_listener(isr_ctrl, &rgb_hist_handle_isr_notifier, listening_bit, dpu_comp);
}

void dpu_comp_rgb_hist_handle_deinit(struct ukmd_isr *isr_ctrl, uint32_t listening_bit)
{
	ukmd_isr_unregister_listener(isr_ctrl, &rgb_hist_handle_isr_notifier, listening_bit);
}