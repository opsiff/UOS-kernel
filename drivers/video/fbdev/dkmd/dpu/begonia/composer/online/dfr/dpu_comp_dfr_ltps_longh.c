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

#include <linux/kthread.h>
#include <linux/spinlock.h>
#include "dpu_comp_dfr_ltps_longh.h"
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

struct dfr_ltps_longh_ctrl {
	bool inited;
	bool te_isr_routine_enabled;
	bool vactive_isr_routine_enabled;
	bool commit_skip_flag;
	bool first_frame;
	spinlock_t commit_flag_spin_lock;
	uint32_t pre_frm_rate;
	uint32_t pfm_mode_status;
	uint32_t vactive_status;
	spinlock_t status_spin_lock;
	uint32_t te_need_skip_num;
	spinlock_t num_spin_lock;
	spinlock_t cur_frm_rate_spin_lock;
	struct dpu_comp_dfr_ctrl *dfr_ctrl;
	struct kthread_work te_mode_swicth_work;
	struct cmdlist_config cmdlist;
};

enum PFM_MODE_STATUS {
	PRE_FPS_60,
	/* Switching mode before switching frames 60hz to high fps */
	PRE_SWICTH_PFM,
	/* Switching frames before switching mode high fps to 60hz */
	PRE_SWICTH_FPS,
};

enum VACTIVE_STATUS{
	VACTIVE_END,
	VACTIVE_START,
};

enum SKIP_FRM_NUM{
	SKIP_ONE_FRM = 1,
	SKIP_TWO_FRM,
};

static struct dfr_ltps_longh_ctrl g_ltps_longh_ctrl = { 0 };

/* panel pfm（Pulse frequency modulation）mode switch Only in 60hz */
static bool dfr_ltps_updt_pfm_mode(struct dpu_comp_dfr_ctrl *dfr_ctrl,
	struct dpu_panel_ops *pops, struct dfr_ltps_longh_ctrl *priv)
{
	/* pre frame high fps, cur frame 60hz need update pfm mode */
	if (dfr_ctrl->pre_frm_rate == dfr_ctrl->cur_frm_rate &&
		priv->pfm_mode_status == PRE_SWICTH_FPS) {
		dpu_pr_debug("pre frame high fps -> 60hz, cur frame is_need_set_pfm");
		pops->update_fps_pfm_mode(dfr_ctrl->cur_frm_rate);
		priv->pfm_mode_status = PRE_FPS_60;
		return true;
	}
	/* next frame high fps, cur frame 60hz need update pfm mode */
	if ((dfr_ctrl->pre_frm_rate != dfr_ctrl->cur_frm_rate) &&
		(dfr_ctrl->pre_frm_rate == 60) && priv->pfm_mode_status == PRE_FPS_60) {
		dpu_pr_debug("frame rate change 60hz -> %u, cur frame is_need_set_pfm",
			dfr_ctrl->cur_frm_rate);
		pops->update_fps_pfm_mode(dfr_ctrl->cur_frm_rate);
		priv->pfm_mode_status = PRE_SWICTH_PFM;
		return true;
	}
	/* cur frame high fps, set flag for next frame 60hz update pfm mode */
	if ((dfr_ctrl->pre_frm_rate != dfr_ctrl->cur_frm_rate) &&
		(dfr_ctrl->cur_frm_rate == 60)) {
		priv->pfm_mode_status = PRE_SWICTH_FPS;
		dpu_pr_debug("frame rate change %u -> 60hz, next frame is_need_set_pfm",
			dfr_ctrl->pre_frm_rate);
	}

	return false;
}

static inline void set_priv_vactive_status(struct dfr_ltps_longh_ctrl *priv,
	uint32_t status)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&priv->status_spin_lock, flags);
	priv->vactive_status = status;
	spin_unlock_irqrestore(&priv->status_spin_lock, flags);
}

static inline void set_priv_commit_skip_flag(struct dfr_ltps_longh_ctrl *priv,
	bool value)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&priv->commit_flag_spin_lock, flags);
	priv->commit_skip_flag = value;
	spin_unlock_irqrestore(&priv->commit_flag_spin_lock, flags);
}

static inline void set_priv_te_need_skip_num(struct dfr_ltps_longh_ctrl *priv,
	int num)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&priv->num_spin_lock, flags);
	priv->te_need_skip_num = (uint32_t)num;
	spin_unlock_irqrestore(&priv->num_spin_lock, flags);
}

static inline void reduce_priv_te_need_skip_num(struct dfr_ltps_longh_ctrl *priv)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&priv->num_spin_lock, flags);
	priv->te_need_skip_num--;
	spin_unlock_irqrestore(&priv->num_spin_lock, flags);
}

static inline void set_dfr_cur_frm_rate(struct dpu_comp_dfr_ctrl *dfr_ctrl,
	struct dfr_ltps_longh_ctrl *priv, uint32_t tar_frm_rate)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&priv->cur_frm_rate_spin_lock, flags);
	dfr_ctrl->cur_frm_rate = tar_frm_rate;
	spin_unlock_irqrestore(&priv->cur_frm_rate_spin_lock, flags);
}

static int32_t dfr_ltps_longh_switch_frm_rate(struct dpu_comp_dfr_ctrl *dfr_ctrl,
	uint32_t frame_rate)
{
	struct dfr_ltps_longh_ctrl *priv = NULL;

	dpu_check_and_return(!dfr_ctrl, -1, err, "dfr_ctrl is null");
	priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	dpu_check_and_return(!priv, -1, err, "priv is null");

	priv->pre_frm_rate = dfr_ctrl->cur_frm_rate;
	set_dfr_cur_frm_rate(dfr_ctrl, priv, frame_rate);
	return 0;
}

static int32_t dfr_ltps_set_skip_frm_num(struct dpu_comp_dfr_ctrl *dfr_ctrl,
	struct dpu_panel_info *dpinfo, struct dfr_ltps_longh_ctrl *priv)
{
	/* longH high to low fps change need skip one frm */
	if (dfr_ctrl->pre_frm_rate > dfr_ctrl->cur_frm_rate) {
		dpu_pr_info("vactive isr %u -> %u, te mode switch, te need skip",
			dfr_ctrl->pre_frm_rate, dfr_ctrl->cur_frm_rate);
		set_priv_te_need_skip_num(priv, SKIP_ONE_FRM);

		/* panel te mode change need skip two frm */
		if (dpinfo->panel_spcl_cfg.lcd_updt_fps_te_support)
			set_priv_te_need_skip_num(priv, SKIP_TWO_FRM);

		dpu_pr_info("priv->te_need_skip_num = %u", priv->te_need_skip_num);
	}
	return 0;
}

static int32_t dfr_ltps_longh_by_mcu_set_skip_frm_num(struct dpu_comp_dfr_ctrl *dfr_ctrl,
	struct dpu_panel_info *pannel_info, struct dfr_ltps_longh_ctrl *priv)
{
	int skip_num = 0;
	/* longH high to low fps change need skip one frm */
	if (dfr_ctrl->pre_frm_rate > dfr_ctrl->cur_frm_rate) {
		dpu_pr_info("vactive isr %u -> %u, te mode switch, te need skip",
			dfr_ctrl->pre_frm_rate, dfr_ctrl->cur_frm_rate);

		skip_num = SKIP_ONE_FRM;

		/* panel te mode change need skip two frm */
		if (pannel_info->panel_spcl_cfg.lcd_updt_fps_te_support)
			skip_num = SKIP_TWO_FRM;
		
		/* prevent collection point before avtive_end */
		skip_num++;
		dpu_pr_info("priv->te_need_skip_num = %u", priv->te_need_skip_num);
	}
	set_priv_te_need_skip_num(priv, skip_num);
	return 0;
}

static void update_frame_rate(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dkmd_connector_info *pinfo = dfr_ctrl->dpu_comp->conn_info;
	struct dpu_panel_ops *pops = get_panel_ops(pinfo->base.id);
	struct dpu_connector *connector = get_primary_connector(pinfo);
	struct dpu_panel_info *pannel_info = pops->get_panel_info();
	struct dfr_ltps_longh_ctrl *priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	char __iomem *dpu_base = dfr_ctrl->dpu_comp->comp_mgr->dpu_base;;

	mipi_dsi_dfr_update(connector, dfr_ctrl->cur_frm_rate, dfr_ctrl->mode);

	if (dfr_ctrl->porch_fps != dfr_ctrl->cur_frm_rate) {
		dfr_ctrl->porch_fps = dfr_ctrl->cur_frm_rate;
		queue_work(dfr_ctrl->dfr_dvfs_notice_wq, &dfr_ctrl->dfr_dvfs_notice_work);
	}

	if (pops->update_lcd_fps)
		pops->update_lcd_fps(dfr_ctrl->cur_frm_rate);

	if (pannel_info->panel_spcl_cfg.lcd_updt_fps_te_support &&
		pops->update_fps_te_mode)
		kthread_queue_work(&dfr_ctrl->dpu_comp->handle_worker,
			&priv->te_mode_swicth_work);

	dfr_ctrl->pre_frm_rate = dfr_ctrl->cur_frm_rate;

	/* force update data in mcu mode during startup finshed */
	dpu_dacc_set_need_wait_te_num(dpu_base, priv->te_need_skip_num);
	dpu_dacc_update_frame_rate_info(dpu_base, dfr_ctrl->cur_frm_rate, dfr_ctrl->cur_frm_rate, 0);
}

static int32_t dfr_ltps_longh_update_frm_rate_isr_handler(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dpu_panel_ops *pops = NULL;
	struct dkmd_connector_info *pinfo = NULL;
	struct dpu_panel_info *pannel_info = NULL;
	struct dfr_ltps_longh_ctrl *priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;;

	pinfo = dfr_ctrl->dpu_comp->conn_info;
	dpu_check_and_return(!pinfo, -1, err, "pinfo is null");

	pops = get_panel_ops(pinfo->base.id);
	dpu_check_and_return(!pops, -1, err, "panel ops is null");

	pannel_info = pops->get_panel_info();
	dpu_check_and_return(!pannel_info, -1, err, "pannel_info is null");
	dpu_check_and_return(!dfr_ctrl->dpu_comp->comp_mgr, -1, err, "comp_mgr is null\n");

	/* vactive end */
	set_priv_vactive_status(priv, VACTIVE_END);

	if (pannel_info->panel_spcl_cfg.lcd_updt_fps_pfm_support &&
		pops->update_fps_pfm_mode) {
		if (dfr_ltps_updt_pfm_mode(dfr_ctrl, pops, priv))
			return 0;
	}

	if (dfr_ctrl->pre_frm_rate == dfr_ctrl->cur_frm_rate) {
		dpu_pr_debug("frame rate do not change");
		return 0;
	}

	if (dfr_ctrl->mode == DFR_MODE_LONGH_BY_MCU)
		dfr_ltps_longh_by_mcu_set_skip_frm_num(dfr_ctrl, pannel_info, priv);
	else
		dfr_ltps_set_skip_frm_num(dfr_ctrl, pannel_info, priv);

	dpu_pr_info("frame rate change from %u to %u", dfr_ctrl->pre_frm_rate,
		dfr_ctrl->cur_frm_rate);
#ifdef CONFIG_POWER_DUBAI
	/* report when lcd fresh rate change */
	HWDUBAI_LOGE("DUBAI_TAG_EPS_LCD_FREQ", "sourcerate=%u targetrate=%u",
		dfr_ctrl->pre_frm_rate, dfr_ctrl->cur_frm_rate);
#endif
	update_frame_rate(dfr_ctrl);

	return 0;
}

static bool is_support_updt_fps_te_mode(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dpu_panel_ops *pops = NULL;
	struct dkmd_connector_info *pinfo = NULL;
	struct dpu_panel_info *dpinfo = NULL;

	dpu_check_and_return(!dfr_ctrl->dpu_comp, false, err, "dpu_comp is null");
	pinfo = dfr_ctrl->dpu_comp->conn_info;
	dpu_check_and_return(!pinfo, false, err, "pinfo is null");

	pops = get_panel_ops(pinfo->base.id);
	dpu_check_and_return(!pops, false, err, "panel ops is null");

	dpinfo = pops->get_panel_info();
	dpu_check_and_return(!dpinfo, false, err, "dpinfo is null");

	if (dpinfo->panel_spcl_cfg.lcd_updt_fps_te_support &&
		pops->update_fps_te_mode)
		return true;

	return false;
}

static bool is_need_skip_commit(struct dpu_comp_dfr_ctrl *dfr_ctrl,
	struct dfr_ltps_longh_ctrl *priv)
{
	/*
	*  case 1:fps change first
	*  need skip commit wait te skip num complete
	*/
	if (priv->commit_skip_flag || priv->te_need_skip_num) {
		dpu_pr_info("skip commit wait te skip complete");
		set_priv_commit_skip_flag(priv, true);
		return true;
	}
	/*
	*  case 2:commit first
	*  high fps switch to low fps rate during display vactive
	*  need skip this commit
	*/
	if ((priv->pre_frm_rate > dfr_ctrl->cur_frm_rate ||
		dfr_ctrl->pre_frm_rate > dfr_ctrl->cur_frm_rate) &&
		priv->vactive_status) {
		dpu_pr_info("skip commit,fps high to low in vactive");
		set_priv_commit_skip_flag(priv, true);
		return true;
	}

	return false;
}

static int32_t dfr_ltps_longh_commit(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_longh_ctrl *priv = NULL;

	dpu_check_and_return(!dfr_ctrl, -1, err, "dfr ctrl is null");
	priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	dpu_check_and_return(!priv, -1, err, "priv is null");
	dpu_check_and_return(!dfr_ctrl->dpu_comp, -1, err, "dpu_comp is null");
	dpu_check_and_return(!dfr_ctrl->dpu_comp->conn_info, -1, err, "conn_info is null");

	priv->first_frame = false;
	if (is_need_skip_commit(dfr_ctrl, priv))
		return 0;

	if (dfr_ctrl->dpu_comp->conn_info->enable_ldi)
		dfr_ctrl->dpu_comp->conn_info->enable_ldi(dfr_ctrl->dpu_comp->conn_info);

	return 0;
}

static void te_mode_switch_handle_work(struct kthread_work *work)
{
	struct dpu_panel_ops *pops = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;
	struct dkmd_connector_info *pinfo = NULL;
	struct dfr_ltps_longh_ctrl *priv = NULL;

	dpu_check_and_no_retval(!work, err, "work is NULL");

	priv = container_of(work, struct dfr_ltps_longh_ctrl, te_mode_swicth_work);
	dpu_check_and_no_retval(!priv, err, "priv is null");

	dfr_ctrl = priv->dfr_ctrl;
	dpu_check_and_no_retval(!dfr_ctrl, err, "dfr_ctrl is null pointer");
	dpu_check_and_no_retval(!dfr_ctrl->dpu_comp, err, "dpu_comp is null pointer");

	pinfo = dfr_ctrl->dpu_comp->conn_info;
	dpu_check_and_no_retval(!pinfo, err, "pinfo is null");

	pops = get_panel_ops(pinfo->base.id);
	dpu_check_and_no_retval(!pops, err, "panel ops is null");

	dpu_pr_info("te_handle_work done");
	if (pops->update_fps_te_mode)
		pops->update_fps_te_mode(dfr_ctrl->cur_frm_rate);
}

static void dfr_te_skip_longh_enable_ldi(struct dpu_comp_dfr_ctrl *dfr_ctrl,
	struct dfr_ltps_longh_ctrl *priv)
{
	dpu_check_and_no_retval(!dfr_ctrl->dpu_comp, err, "dpu_comp is null");
	dpu_check_and_no_retval(!dfr_ctrl->dpu_comp->conn_info, err, "conn_info is null");

	if (dfr_ctrl->dpu_comp->conn_info->enable_ldi)
		dfr_ctrl->dpu_comp->conn_info->enable_ldi(dfr_ctrl->dpu_comp->conn_info);

	set_priv_commit_skip_flag(priv, false);
}

static int32_t dfr_te_skip_longh_te_isr_notify(struct notifier_block *self,
	unsigned long action, void *data)
{
	struct ukmd_listener_data *listener_data = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;
	struct dfr_ltps_longh_ctrl *priv = NULL;

	dpu_check_and_return(!data, -1, err, "data is null pointer");
	listener_data = (struct ukmd_listener_data *)data;
	dfr_ctrl = (struct dpu_comp_dfr_ctrl *)(listener_data->data);
	dpu_check_and_return(!dfr_ctrl, -1, err, "dfr_ctrl is null");
	priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	dpu_check_and_return(!priv, -1, err, "priv is null");

	if (!priv->te_isr_routine_enabled)
		return 0;

	if (!priv->te_need_skip_num) {
		if (priv->commit_skip_flag) {
			dpu_pr_info("frame rate switch commit skip success, set sfu in te isr");
			dfr_te_skip_longh_enable_ldi(dfr_ctrl, priv);
		}
		dpu_pr_debug("te is not skip, do not set sfu in te isr");
		return 0;
	}

	/* te is skipped, then set sfu */
	reduce_priv_te_need_skip_num(priv);
	dpu_pr_info("te need skip, priv->te_need_skip_num = %u",
		priv->te_need_skip_num);
	if ((priv->te_need_skip_num == 0) && priv->commit_skip_flag)
		dfr_te_skip_longh_enable_ldi(dfr_ctrl, priv);

	return 0;
}

static struct notifier_block g_te_isr_notifier = {
	.notifier_call = dfr_te_skip_longh_te_isr_notify,
};

static void dfr_te_skip_register_te_isr(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	uint32_t dsi_te_id;
	struct ukmd_isr *isr_ctrl = NULL;
	struct dkmd_connector_info *pinfo = NULL;

	if (dfr_ctrl->mode == DFR_MODE_LONGH_BY_MCU)
		return;
	dpu_check_and_no_retval(!dfr_ctrl->dpu_comp, err, "dpu_comp is null");
	pinfo = dfr_ctrl->dpu_comp->conn_info;
	dpu_check_and_no_retval(!pinfo, err, "pinfo is null");

	isr_ctrl = &dfr_ctrl->dpu_comp->isr_ctrl;
	dsi_te_id = pinfo->base.lcd_te_idx == 0 ? DSI_INT_LCD_TE0 : DSI_INT_LCD_TE1;
	ukmd_isr_register_listener(isr_ctrl, &g_te_isr_notifier, dsi_te_id, dfr_ctrl);
}

static int32_t dfr_longh_vactive_start_isr_notify(struct notifier_block *self,
	unsigned long action, void *data)
{
	struct ukmd_listener_data *listener_data = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;
	struct dfr_ltps_longh_ctrl *priv = NULL;

	dpu_check_and_return(!data, -1, err, "data is null pointer");
	listener_data = (struct ukmd_listener_data *)data;
	dfr_ctrl = (struct dpu_comp_dfr_ctrl *)(listener_data->data);
	dpu_check_and_return(!dfr_ctrl, -1, err, "dfr_ctrl is null");
	priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	dpu_check_and_return(!priv, -1, err, "priv is null");

	if (!priv->vactive_isr_routine_enabled)
		return 0;

	/* vactive start */
	set_priv_vactive_status(priv, VACTIVE_START);
	return 0;
}

static struct notifier_block g_vactive_start_isr_notifier = {
	.notifier_call = dfr_longh_vactive_start_isr_notify,
};

static void dfr_longh_register_vactive_start_isr(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct ukmd_isr *isr_ctrl = NULL;

	dpu_check_and_no_retval(!dfr_ctrl->dpu_comp, err, "dpu_comp is null");

	isr_ctrl = &dfr_ctrl->dpu_comp->isr_ctrl;
	ukmd_isr_register_listener(isr_ctrl, &g_vactive_start_isr_notifier,
		DSI_INT_VACT0_START, dfr_ctrl);
}

static void dfr_ltps_longh_setup_priv_data(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_longh_ctrl *priv = NULL;

	dpu_check_and_no_retval(!dfr_ctrl, err, "dfr_ctrl is null");

	dfr_ctrl->priv_data = (void*)&g_ltps_longh_ctrl;
	priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	priv->pre_frm_rate = dfr_ctrl->pre_frm_rate;
	priv->vactive_status = 0;
	priv->pfm_mode_status = PRE_FPS_60;
	spin_lock_init(&(priv->status_spin_lock));
	priv->te_need_skip_num = 0;
	spin_lock_init(&(priv->num_spin_lock));
	spin_lock_init(&(priv->cur_frm_rate_spin_lock));
	priv->commit_skip_flag = false;
	spin_lock_init(&(priv->commit_flag_spin_lock));
	priv->dfr_ctrl = dfr_ctrl;
	dfr_ctrl->vsync_offset_threshold = 1500;
	priv->first_frame = true;

	if (!priv->inited) {
		priv->inited = true;
		dfr_te_skip_register_te_isr(dfr_ctrl);
		dfr_longh_register_vactive_start_isr(dfr_ctrl);
		if (is_support_updt_fps_te_mode(dfr_ctrl))
			kthread_init_work(&priv->te_mode_swicth_work,
				te_mode_switch_handle_work);
	}

	priv->te_isr_routine_enabled = true;
	priv->vactive_isr_routine_enabled = true;
}

static void dfr_ltps_longh_setup_priv_data_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_longh_ctrl *priv = NULL;

	dfr_ltps_longh_setup_priv_data(dfr_ctrl);
	priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	dpu_dacc_setup_priv_data_by_mcu(dfr_ctrl, &priv->cmdlist);
}

static void dfr_ltps_longh_release_data(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_longh_ctrl *priv = NULL;

	dpu_check_and_no_retval(!dfr_ctrl, err, "dfr_ctrl is null");
	priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	dpu_check_and_no_retval(!priv, err, "priv is null");

	if (dfr_ctrl->pre_frm_rate != dfr_ctrl->cur_frm_rate) {
		dpu_pr_info("pre_frm_rate %u is not equal cur_frm_rate %u",
			dfr_ctrl->pre_frm_rate, dfr_ctrl->cur_frm_rate);
		set_dfr_cur_frm_rate(dfr_ctrl, priv, dfr_ctrl->pre_frm_rate);
	}

	priv->te_isr_routine_enabled = false;
	priv->vactive_isr_routine_enabled = false;
}

static void dfr_ltps_longh_release_data_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_longh_ctrl *priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	dfr_ltps_longh_release_data(dfr_ctrl);
	dpu_dacc_release_data_by_mcu(dfr_ctrl, &priv->cmdlist);
}

static int32_t dfr_ltps_longh_commit_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_longh_ctrl *priv = (struct dfr_ltps_longh_ctrl *)dfr_ctrl->priv_data;
	priv->first_frame = false;
	return dpu_dacc_commit_by_mcu(dfr_ctrl, &priv->cmdlist);
}

static int32_t dfr_ltps_longh_send_dcs_cmds(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct disp_effect_params *effect_params)
{
	struct dkmd_connector_info *pinfo = NULL;
	struct dpu_bl_ctrl *bl_ctrl = NULL;

	dpu_check_and_return(!dfr_ctrl, -1, err, "dfr_ctrl is null");
	dpu_check_and_return(!dfr_ctrl->dpu_comp, -1, err, "dpu_composer is null");
	dpu_check_and_return(effect_params->effect_num > EFFECT_PARAMS_MAX_NUM, -1, err, "effect num exceeds max num");
	if ((effect_params->delay != 0 || effect_params->effect_num == 0) && (g_debug_dpu_send_dcs_cmds == 0)) {
		dpu_pr_debug("Nothing to send !\n");
		return 0;
	}

	bl_ctrl = &dfr_ctrl->dpu_comp->bl_ctrl;
	pinfo = dfr_ctrl->dpu_comp->conn_info;
	dpu_check_and_return(!pinfo, -1, err, "pinfo is null");

	dpu_pr_info("set bl_level level with frame+\n");
	dpu_backlight_update_level(bl_ctrl, effect_params);
	pipeline_next_ops_handle(pinfo->conn_device, pinfo, SET_BACKLIGHT, &bl_ctrl->bl_level);
	dpu_pr_info("set bl_level level with frame-, bl_level is %u\n", bl_ctrl->bl_level);

	return 0;
}

static bool dfr_ltps_is_fisrt_frame(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	struct dfr_ltps_longh_ctrl *priv = dfr_ctrl->priv_data;
	return priv->first_frame;
}

static struct dfr_ctrl_ops g_dfr_ctrl_ops = {
	.switch_frm_rate = dfr_ltps_longh_switch_frm_rate,
	.send_dcs_cmds_with_frm = dfr_ltps_longh_send_dcs_cmds,
	.update_frm_rate_isr_handler = dfr_ltps_longh_update_frm_rate_isr_handler,
	.is_fisrt_frame = dfr_ltps_is_fisrt_frame,
};

void dfr_ltps_longh_register_ops(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	dpu_multi_present_init(&dfr_ctrl->dpu_comp->multi_present_ctrl, DFR_MODE_INVALID);
	g_dfr_ctrl_ops.commit = dfr_ltps_longh_commit;
	g_dfr_ctrl_ops.release_data = dfr_ltps_longh_release_data;
	g_dfr_ctrl_ops.setup_data = dfr_ltps_longh_setup_priv_data,
	dfr_ctrl->ops = &g_dfr_ctrl_ops;
}

void dfr_ltps_longh_register_ops_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	dpu_multi_present_init(&dfr_ctrl->dpu_comp->multi_present_ctrl, dfr_ctrl->mode);
	g_dfr_ctrl_ops.commit = dfr_ltps_longh_commit_by_mcu;
	g_dfr_ctrl_ops.release_data = dfr_ltps_longh_release_data_by_mcu;
	g_dfr_ctrl_ops.setup_data = dfr_ltps_longh_setup_priv_data_by_mcu;
	dfr_ctrl->ops = &g_dfr_ctrl_ops;
}