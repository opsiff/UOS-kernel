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

#include "dpu_comp_dfr_ltps.h"
#include "dpu_comp_mgr.h"
#include "panel_mgr.h"
#include "dpu_conn_mgr.h"
#include "mipi_dsi_dev.h"
#include "cmdlist_interface.h"
#include "ukmd_cmdlist.h"
#ifdef CONFIG_POWER_DUBAI
#include <huawei_platform/log/hwlog_kernel.h>
#endif
#include "config/dpu_comp_dfr_config_utils.h"

struct dfr_ltps_ctrl {
	bool is_lock_init;
	bool first_frame;
	spinlock_t switch_frm_lock;
	struct cmdlist_config cmdlist;
};

static struct dfr_ltps_ctrl g_dfr_ltps_ctrl;

static void dfr_ltps_setup_data(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_ctrl *priv = NULL;

	priv = &g_dfr_ltps_ctrl;
	if (!priv->is_lock_init) {
		priv->is_lock_init = true;
		spin_lock_init(&priv->switch_frm_lock);
	}
	priv->first_frame = true;

	dfr_ctrl->priv_data = priv;
	dfr_ctrl->vsync_offset_threshold = 1500;
}

static void dfr_ltps_setup_data_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_ctrl *priv = NULL;

	dfr_ltps_setup_data(dfr_ctrl);
	priv = (struct dfr_ltps_ctrl *)dfr_ctrl->priv_data;
	dpu_dacc_setup_priv_data_by_mcu(dfr_ctrl, &priv->cmdlist);
}

static void dfr_ltps_release_data(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_ctrl *priv = (struct dfr_ltps_ctrl *)dfr_ctrl->priv_data;
	dpu_dacc_release_data_by_mcu(dfr_ctrl, &priv->cmdlist);
}

static int32_t dfr_ltps_switch_frm_rate(struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t frame_rate)
{
	unsigned long flags = 0;
	struct dfr_ltps_ctrl *priv = (struct dfr_ltps_ctrl *)dfr_ctrl->priv_data;

	spin_lock_irqsave(&priv->switch_frm_lock, flags);
	dfr_ctrl->cur_frm_rate = frame_rate;
	dfr_ctrl->cur_te_rate = dfr_ctrl->cur_frm_rate;
	dpu_pr_debug("dfr_ltps_switch_frm_rate. pre_frm_rate: %d, cur_frm_rate: %d.",
		dfr_ctrl->pre_frm_rate, dfr_ctrl->cur_frm_rate);
	spin_unlock_irqrestore(&priv->switch_frm_lock, flags);

	return 0;
}

static int32_t dfr_ltps_update_frm_rate_isr_handler(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	unsigned long flags = 0;
	struct dpu_panel_ops *pops = NULL;
	struct dpu_connector *connector = NULL;
	struct dkmd_connector_info *pinfo = NULL;
	struct dfr_ltps_ctrl *priv = NULL;
	struct dpu_panel_info *dpinfo = NULL;
	struct comp_online_present *present = NULL;
	char __iomem *dpu_base = NULL;
	uint32_t cur_te_rate = 0;

	pinfo = dfr_ctrl->dpu_comp->conn_info;
	dpu_check_and_return(!pinfo, -1, err, "pinfo is null");

	priv = (struct dfr_ltps_ctrl *)dfr_ctrl->priv_data;
	dpu_check_and_return(!priv, -1, err, "priv is null");

	present = (struct comp_online_present *)dfr_ctrl->dpu_comp->present_data;
	dpu_check_and_return(!present, -1, err, "present is null");

	pops = get_panel_ops(pinfo->base.id);
	dpu_check_and_return(!pops, -1, err, "pops is null");

	dpinfo = pops->get_panel_info();
	dpu_check_and_return(!dpinfo, -1, err, "dpinfo is null");
	dpu_check_and_return(!dfr_ctrl->dpu_comp->comp_mgr, -1, err, "comp_mgr is null");

	spin_lock_irqsave(&priv->switch_frm_lock, flags);

	if ((dpinfo->panel_force_update_frm_rate > 0) &&
		(present->frames[present->displayed_idx].in_frame.frame_index > 0)) {
		dpu_pr_info("force update frame rate during startup");
	} else if (dfr_ctrl->pre_frm_rate == dfr_ctrl->cur_frm_rate) {
		dpu_pr_debug("frame rate do not change. pre_frm_rate: %d, cur_frm_rate: %d.",
			dfr_ctrl->pre_frm_rate, dfr_ctrl->cur_frm_rate);
		spin_unlock_irqrestore(&priv->switch_frm_lock, flags);
		return 0;
	}

	dpu_pr_info("frame rate change from %d to %d", dfr_ctrl->pre_frm_rate, dfr_ctrl->cur_frm_rate);
#ifdef CONFIG_POWER_DUBAI
	/* report when lcd fresh rate change */
	HWDUBAI_LOGE("DUBAI_TAG_EPS_LCD_FREQ", "sourcerate=%u targetrate=%u",
		dfr_ctrl->pre_frm_rate, dfr_ctrl->cur_frm_rate);
#endif

	connector = get_primary_connector(pinfo);
	mipi_dsi_dfr_update(connector, dfr_ctrl->cur_frm_rate, dfr_ctrl->mode);

	if (pops->update_lcd_fps)
		pops->update_lcd_fps(dfr_ctrl->cur_frm_rate);

	dfr_ctrl->pre_frm_rate = dfr_ctrl->cur_frm_rate;
	cur_te_rate = dfr_ctrl->cur_frm_rate;
	/* force update frame rate during startup finshed */
	dpinfo->panel_force_update_frm_rate = 0;
	dpu_base = dfr_ctrl->dpu_comp->comp_mgr->dpu_base;
	dpu_dacc_update_frame_rate_info(dpu_base, dfr_ctrl->cur_frm_rate, cur_te_rate, 0);

	spin_unlock_irqrestore(&priv->switch_frm_lock, flags);

	return 0;
}


static int32_t dfr_ltps_commit(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_ctrl *priv = dfr_ctrl->priv_data;
	if (dfr_ctrl->dpu_comp->conn_info->enable_ldi)
		dfr_ctrl->dpu_comp->conn_info->enable_ldi(dfr_ctrl->dpu_comp->conn_info);
	priv->first_frame = false;
	return 0;
}

static int32_t dfr_ltps_commit_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_ctrl *priv = (struct dfr_ltps_ctrl *)dfr_ctrl->priv_data;
	priv->first_frame = false;
	return dpu_dacc_commit_by_mcu(dfr_ctrl, &priv->cmdlist);
}

static int32_t dfr_ltps_send_dcs_cmds(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct disp_effect_params *effect_params)
{
	struct dkmd_connector_info *pinfo = NULL;
	struct dpu_bl_ctrl *bl_ctrl = NULL;

	dpu_check_and_return(!dfr_ctrl->dpu_comp, -1, err, "dpu_composer is null");
	dpu_check_and_return(effect_params->effect_num > EFFECT_PARAMS_MAX_NUM, -1, err, "effect num exceeds max num");
	if ((effect_params->delay != 0 || effect_params->effect_num == 0) && (g_debug_dpu_send_dcs_cmds == 0)) {
		dpu_pr_debug("Nothing to send !");
		return 0;
	}

	bl_ctrl = &dfr_ctrl->dpu_comp->bl_ctrl;
	pinfo = dfr_ctrl->dpu_comp->conn_info;
	dpu_check_and_return(!pinfo, -1, err, "pinfo is null");

	dpu_pr_info("set bl_level level with frame+");
	dpu_backlight_update_level(bl_ctrl, effect_params);
	pipeline_next_ops_handle(pinfo->conn_device, pinfo, SET_BACKLIGHT, &bl_ctrl->bl_level);
	dpu_pr_info("set bl_level level with frame-, bl_level is %u", bl_ctrl->bl_level);

	return 0;
}

static bool dfr_ltps_is_fisrt_frame(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_ctrl *priv = dfr_ctrl->priv_data;
	return priv->first_frame;
}

static struct dfr_ctrl_ops g_dfr_ctrl_ops = {
	.setup_data = dfr_ltps_setup_data,
	.switch_frm_rate = dfr_ltps_switch_frm_rate,
	.commit = dfr_ltps_commit,
	.send_dcs_cmds_with_frm = dfr_ltps_send_dcs_cmds,
	.update_frm_rate_isr_handler = dfr_ltps_update_frm_rate_isr_handler,
	.is_fisrt_frame = dfr_ltps_is_fisrt_frame,
};

static struct dfr_ctrl_ops g_dfr_ctrl_ops_by_mcu = {
	.setup_data = dfr_ltps_setup_data_by_mcu,
	.switch_frm_rate = dfr_ltps_switch_frm_rate,
	.commit = dfr_ltps_commit_by_mcu,
	.send_dcs_cmds_with_frm = dfr_ltps_send_dcs_cmds,
	.update_frm_rate_isr_handler = dfr_ltps_update_frm_rate_isr_handler,
	.release_data = dfr_ltps_release_data,
	.is_fisrt_frame = dfr_ltps_is_fisrt_frame,
};

void dfr_ltps_register_ops(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	// not support multi present
	dpu_multi_present_init(&dfr_ctrl->dpu_comp->multi_present_ctrl, DFR_MODE_INVALID);
	dfr_ctrl->ops = &g_dfr_ctrl_ops;
}

void dfr_ltps_register_ops_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	dpu_multi_present_init(&dfr_ctrl->dpu_comp->multi_present_ctrl, dfr_ctrl->mode);
	dfr_ctrl->ops = &g_dfr_ctrl_ops_by_mcu;
}