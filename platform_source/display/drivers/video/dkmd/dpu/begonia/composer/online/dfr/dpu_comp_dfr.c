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
#include "dpu_comp_dfr.h"
#include "dpu_comp_dfr_constant.h"
#include "dpu_comp_dfr_te_skip.h"
#include "dpu_comp_dfr_te_skip_acpu.h"
#include "dpu_comp_dfr_ltps.h"
#include "dpu_comp_dfr_ltps_longh.h"
#include "dpu_comp_mgr.h"
#include "dkmd_dfr_info.h"
#include "dpu_conn_mgr.h"
#ifdef CONFIG_POWER_DUBAI
#include <huawei_platform/log/hwlog_kernel.h>
#endif

#ifndef array_len
#define array_len(array) (sizeof(array) / sizeof((array)[0]))
#endif

#define FPS_60HZ 60
#define FPS_120HZ 120
#define FPS_360HZ 360

void dpu_comp_dfr_ctrl_setup(struct dpu_composer *dpu_comp, struct comp_online_present *present)
{
	struct dpu_comp_dfr_ctrl* dfr_ctrl = &present->dfr_ctrl;
	struct dfr_info *dfr_info = dkmd_get_dfr_info(dpu_comp->conn_info);

	dpu_pr_info("comp %d, +",dpu_comp->comp.index);
	if (!dfr_info) {
		dpu_pr_info("dfr info is null, use permanent mode default");
		dfr_ctrl->mode = DFR_MODE_CONSTANT;
	} else {
		dfr_ctrl->mode = dfr_info->dfr_mode;
	}

	dfr_ctrl->dpu_comp = dpu_comp;
	dfr_ctrl->cur_safe_frm_rate = 1;
	present->frame_rate = FPS_60HZ;

	dpu_pr_info("dfr mode %d\n", dfr_ctrl->mode);
	switch (dfr_ctrl->mode) {
	case DFR_MODE_CONSTANT:
		dfr_constant_register_ops(dfr_ctrl);
		break;
	case DFR_MODE_TE_SKIP_BY_MCU:
		present->frame_rate = FPS_120HZ;
		dfr_te_skip_register_ops(dfr_ctrl);
		break;
	case DFR_MODE_TE_SKIP_BY_ACPU:
		dfr_te_skip_acpu_register_ops(dfr_ctrl);
		break;
	case DFR_MODE_LONGH_TE_SKIP_BY_MCU:
		present->frame_rate = FPS_120HZ;
		dfr_longh_te_skip_register_ops(dfr_ctrl);
		break;
	case DFR_MODE_LONG_V:
	case DFR_MODE_LONG_VH:
		present->frame_rate = dpu_comp->conn_info->base.fps;
		dfr_ctrl->cur_frm_rate = dpu_comp->conn_info->base.fps;
		dfr_ctrl->pre_frm_rate = dpu_comp->conn_info->base.fps;
		dfr_ltps_register_ops(dfr_ctrl);

#ifdef CONFIG_POWER_DUBAI
		/* mipi fps rate control init */
		HWDUBAI_LOGE("DUBAI_TAG_EPS_LCD_FREQ", "sourcerate=%d targetrate=%d",
			dfr_ctrl->pre_frm_rate, dfr_ctrl->cur_frm_rate);
#endif

		break;
	case DFR_MODE_LONG_H:
		present->frame_rate = dpu_comp->conn_info->base.fps;
		dfr_ctrl->cur_frm_rate = dpu_comp->conn_info->base.fps;
		dfr_ctrl->pre_frm_rate = dpu_comp->conn_info->base.fps;
		dfr_ltps_longh_register_ops(dfr_ctrl);
		break;
	case DFR_MODE_LONGV_BY_MCU:
		present->frame_rate = dpu_comp->conn_info->base.fps;
		dfr_ctrl->cur_frm_rate = dpu_comp->conn_info->base.fps;
		dfr_ctrl->pre_frm_rate = dpu_comp->conn_info->base.fps;
		dfr_ltps_register_ops_by_mcu(dfr_ctrl);
		break;
	case DFR_MODE_LONGH_BY_MCU:
		present->frame_rate = dpu_comp->conn_info->base.fps;
		dfr_ctrl->cur_frm_rate = dpu_comp->conn_info->base.fps;
		dfr_ctrl->pre_frm_rate = dpu_comp->conn_info->base.fps;
		dfr_ltps_longh_register_ops_by_mcu(dfr_ctrl);
		break;
	default:
		dpu_pr_warn("unsupported dfr mode %d, force to use permanent mode", dfr_ctrl->mode);
		dfr_constant_register_ops(dfr_ctrl);
		break;
	}
	dpu_pr_info("comp %d, -", dpu_comp->comp.index);
}

static struct dpu_comp_dfr_ctrl * dkmd_dfr_get_dfr_ctrl_ptr(struct dkmd_connector_info *pinfo)
{
	struct composer *comp = NULL;
	struct dpu_composer *dpu_comp = NULL;
	struct comp_online_present *present = NULL;

	if (!pinfo || !pinfo->base.comp_obj_info) {
		dpu_pr_err("pinfo or comp_obj_info is nullptr!");
		return NULL;
	}

	comp = container_of(pinfo->base.comp_obj_info, struct composer, base);
	if (!comp) {
		dpu_pr_err("comp is nullptr!");
		return NULL;
	}

	dpu_comp = to_dpu_composer(comp);
	if (!dpu_comp) {
		dpu_pr_err("dpu_comp is nullptr!");
		return NULL;
	}
	present = (struct comp_online_present *)dpu_comp->present_data;
	if (!present) {
		dpu_pr_err("present is nullptr!");
		return NULL;
	}

	return &present->dfr_ctrl;
}

static bool check_connector_status(struct dpu_connector *connector)
{
	struct composer *comp = NULL;

	if (!connector->conn_info || !connector->conn_info->base.comp_obj_info) {
		dpu_pr_err("pinfo or comp_obj_info is nullptr!");
		return false;
	}
	comp = container_of(connector->conn_info->base.comp_obj_info, struct composer, base);
	if (!comp) {
		dpu_pr_err("comp is nullptr!");
		return false;
	}

	if (!comp->power_on) {
		dpu_pr_warn("is not power_on!");
		return false;
	}

	return true;
}

int32_t dkmd_exit_current_vsync_idle_period(uint32_t conn_id, uint32_t panel_type)
{
	struct dpu_connector *connector0 = NULL;

	connector0 = get_real_connector(conn_id, panel_type);
	if ((!connector0) || (!connector0->connector_base)) {
		dpu_pr_err("connector_id0=%u is not available!\n", conn_id);
		return -1;
	}

	if (!check_connector_status(connector0))
		return -1;

	composer_active_vsync(connector0->conn_info, true);
	composer_active_vsync(connector0->conn_info, false);
	return 0;
}

int32_t dkmd_dfr_send_refresh(uint32_t conn_id, uint32_t panel_type)
{
	struct dpu_connector *connector0 = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;

	connector0 = get_real_connector(conn_id, panel_type);
	if ((!connector0) || (!connector0->connector_base)) {
		dpu_pr_err("connector_id0=%u is not available!\n", conn_id);
		return -1;
	}

	if (!check_connector_status(connector0))
		return -1;

	composer_active_vsync(connector0->conn_info, true);
	dfr_ctrl = dkmd_dfr_get_dfr_ctrl_ptr(connector0->conn_info);
	if (dfr_ctrl && dfr_ctrl->ops && dfr_ctrl->ops->send_refresh)
		dfr_ctrl->ops->send_refresh(dfr_ctrl);
	composer_active_vsync(connector0->conn_info, false);

	return 0;
}

static void dfr_send_safe_frm_rate(struct dpu_connector *connector, struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t safe_frm_rate)
{
	composer_active_vsync(connector->conn_info, true);
	if (dfr_ctrl && dfr_ctrl->ops && dfr_ctrl->ops->send_safe_frm_rate)
		dfr_ctrl->ops->send_safe_frm_rate(dfr_ctrl, safe_frm_rate);
	composer_active_vsync(connector->conn_info, false);
}

static int32_t dfr_send_safe_frm_rate_with_lock(struct dpu_connector *connector, struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t safe_frm_rate)
{
	if ((!dfr_ctrl) || (!dfr_ctrl->dpu_comp) || (!dfr_ctrl->dpu_comp->comp_mgr)) {
		dpu_pr_warn("dfr_ctrl or dpu_comp or comp_mgr is not available!\n");
		return -1;
	}

	down(&dfr_ctrl->dpu_comp->comp_mgr->power_sem);
	if (!composer_check_power_status(dfr_ctrl->dpu_comp)) {
		dpu_pr_warn("composer %u, panel power off!", dfr_ctrl->dpu_comp->comp.index);
		up(&dfr_ctrl->dpu_comp->comp_mgr->power_sem);
		return 0;
	}

	dfr_send_safe_frm_rate(connector, dfr_ctrl, safe_frm_rate);

	up(&dfr_ctrl->dpu_comp->comp_mgr->power_sem);

	return 0;
}

int32_t dkmd_dfr_send_safe_frm_rate(uint32_t conn_id, uint32_t panel_type, uint32_t safe_frm_rate, const struct sfr_info *sfr_info)
{
	struct dpu_connector *connector0 = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;
	bool is_de_invoked_directly = ((panel_type != PANEL_DP) && (sfr_info == NULL));

	connector0 = get_real_connector(conn_id, panel_type);
	if ((!connector0) || (!connector0->connector_base)) {
		dpu_pr_err("connector_id0=%u is not available!\n", conn_id);
		return -1;
	}

	dfr_ctrl = dkmd_dfr_get_dfr_ctrl_ptr(connector0->conn_info);

	if (is_de_invoked_directly)
		return dfr_send_safe_frm_rate_with_lock(connector0, dfr_ctrl, safe_frm_rate);
	else
		dfr_send_safe_frm_rate(connector0, dfr_ctrl, safe_frm_rate);

	return 0;
}

int32_t dkmd_dfr_enable_dimming(uint32_t conn_id, uint32_t panel_type)
{
	struct dpu_connector *connector0 = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;

	connector0 = get_real_connector(conn_id, panel_type);
	if ((!connector0) || (!connector0->connector_base)) {
		dpu_pr_err("connector_id0=%u is not available!\n", conn_id);
		return -1;
	}

	if (!check_connector_status(connector0))
		return -1;

	composer_active_vsync(connector0->conn_info, true);
	dfr_ctrl = dkmd_dfr_get_dfr_ctrl_ptr(connector0->conn_info);
	if (dfr_ctrl && dfr_ctrl->ops && dfr_ctrl->ops->enable_dimming)
		dfr_ctrl->ops->enable_dimming(dfr_ctrl);
	composer_active_vsync(connector0->conn_info, false);

	return 0;
}

int32_t dkmd_dfr_disable_dimming(uint32_t conn_id, uint32_t panel_type)
{
	struct dpu_connector *connector0 = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;

	connector0 = get_real_connector(conn_id, panel_type);
	if ((!connector0) || (!connector0->connector_base)) {
		dpu_pr_err("connector_id0=%u is not available!\n", conn_id);
		return -1;
	}

	if (!check_connector_status(connector0))
		return -1;

	composer_active_vsync(connector0->conn_info, true);
	dfr_ctrl = dkmd_dfr_get_dfr_ctrl_ptr(connector0->conn_info);
	if (dfr_ctrl && dfr_ctrl->ops && dfr_ctrl->ops->disable_dimming)
		dfr_ctrl->ops->disable_dimming(dfr_ctrl);
	composer_active_vsync(connector0->conn_info, false);

	return 0;
}

int32_t dkmd_dfr_enable_safe_frm_rate(uint32_t conn_id, uint32_t panel_type)
{
	struct dpu_connector *connector0 = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;

	connector0 = get_real_connector(conn_id, panel_type);
	if ((!connector0) || (!connector0->connector_base)) {
		dpu_pr_err("connector_id0=%u is not available!\n", conn_id);
		return -1;
	}
	if (!check_connector_status(connector0))
		return -1;

	composer_active_vsync(connector0->conn_info, true);
	dfr_ctrl = dkmd_dfr_get_dfr_ctrl_ptr(connector0->conn_info);
	if (dfr_ctrl && dfr_ctrl->ops && dfr_ctrl->ops->enable_safe_frm_rate)
		dfr_ctrl->ops->enable_safe_frm_rate(dfr_ctrl);
	composer_active_vsync(connector0->conn_info, false);
	return 0;
}

int32_t dkmd_dfr_disable_safe_frm_rate(uint32_t conn_id, uint32_t panel_type, int32_t disable_type)
{
	struct dpu_connector *connector0 = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;

	connector0 = get_real_connector(conn_id, panel_type);
	if ((!connector0) || (!connector0->connector_base)) {
		dpu_pr_err("connector_id0=%u is not available!\n", conn_id);
		return -1;
	}
	if (!check_connector_status(connector0))
		return -1;

	composer_active_vsync(connector0->conn_info, true);
	dfr_ctrl = dkmd_dfr_get_dfr_ctrl_ptr(connector0->conn_info);
	if (dfr_ctrl && dfr_ctrl->ops && dfr_ctrl->ops->disable_safe_frm_rate)
		dfr_ctrl->ops->disable_safe_frm_rate(dfr_ctrl, disable_type);
	composer_active_vsync(connector0->conn_info, false);
	return 0;
}

void dpu_comp_dfr_ctrl_process(struct dpu_composer *dpu_comp, struct comp_online_present *present,
	struct disp_frame *frame)
{
	present->dfr_ctrl.ppu_ctrl.panel_refresh_ctrl = frame->panel_refresh_ctrl;

	if (present->dfr_ctrl.ops && present->dfr_ctrl.ops->switch_frm_rate) {
		present->dfr_ctrl.ops->switch_frm_rate(&present->dfr_ctrl, present->frame_rate);
		present->comp_maintain.cur_te_rate = present->dfr_ctrl.cur_te_rate;
	}

	if (present->dfr_ctrl.ops && present->dfr_ctrl.ops->send_dcs_cmds_with_frm)
		(void)present->dfr_ctrl.ops->send_dcs_cmds_with_frm(&present->dfr_ctrl, &frame->effect_params);

	if (present->dfr_ctrl.ops && present->dfr_ctrl.ops->commit) {
		present->dfr_ctrl.frame_id = frame->frame_id;
		present->dfr_ctrl.request_time_manos = frame->request_time_nanos;
		present->dfr_ctrl.time_nanos_type = frame->time_nanos_type;
		present->dfr_ctrl.ops->commit(&present->dfr_ctrl);
	}
}

static uint32_t get_porch_fps(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	/* get fps config in lcdkit3.0 */
	uint32_t i;
	uint32_t fps_sup_max = 0;
	struct dfr_info *dinfo = dkmd_get_dfr_info(dfr_ctrl->dpu_comp->conn_info);
	dpu_check_and_return(!dinfo, 0, err, "dfr info is null");

	for (i = 0; i < dinfo->oled_info.fps_sup_num; i++)
		if (dinfo->oled_info.fps_sup_seq[i] > fps_sup_max)
			fps_sup_max = dinfo->oled_info.fps_sup_seq[i];

	if (dinfo->oled_info.porch_fps_num != dinfo->oled_info.fps_sup_num) {
		dpu_pr_info("not support longh dvfs, return fps_sup_max = %u", fps_sup_max);
		return fps_sup_max;
	}
	
	for (i = 0; i < dinfo->oled_info.fps_sup_num; i++) {
		if (dinfo->oled_info.fps_sup_seq[i] == dfr_ctrl->porch_fps) {
			dpu_pr_info("find porch_fps in porch_fps_seq: %u", dinfo->oled_info.porch_fps_seq[i]);
			return dinfo->oled_info.porch_fps_seq[i];
		}
	}
	dpu_pr_warn("failed to find porch_fps in porch_fps_seq");

	return 0;
}
 
static void dpu_dfr_porch_fps_update_uevent(struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t porch_fps)
{
 	char buf[64] = {0};  /* dfr notify print info */
 	char *envp[2] = {NULL};  /* environment variable */
	int ret;
	struct composer *comp = NULL;
 
 	ret = snprintf_s(buf, sizeof(buf), sizeof(buf) - 1, "PORCH_FPS=%u", porch_fps);
	if (ret < 0) {
		dpu_pr_err("format string failed, truncation occurs");
		return;
	}
 
 	envp[0] = buf;
 	envp[1] = NULL;
	comp = &dfr_ctrl->dpu_comp->comp;

 	kobject_uevent_env(&(comp->base.peri_device->dev.kobj), KOBJ_CHANGE, envp);
}
 
void dpu_dfr_dvfs_notice_handle_func(struct work_struct *work)
{
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;
	uint32_t porch_fps = 0;
 
	dfr_ctrl = container_of(work, struct dpu_comp_dfr_ctrl, dfr_dvfs_notice_work);
	dpu_check_and_no_retval(!dfr_ctrl, err, "dfr_ctrl is null");
 
	porch_fps = get_porch_fps(dfr_ctrl);
	dpu_check_and_no_retval(porch_fps == 0, err, "invalid porch_fps");
 
	dpu_dfr_porch_fps_update_uevent(dfr_ctrl, porch_fps);
}

void dpu_dfr_dvfs_notice_register(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	if (dfr_ctrl->mode != DFR_MODE_LONG_H && dfr_ctrl->mode != DFR_MODE_LONG_VH &&
			dfr_ctrl->mode != DFR_MODE_LONGH_TE_SKIP_BY_MCU && dfr_ctrl->mode != DFR_MODE_LONGH_BY_MCU)
		return;

	dfr_ctrl->dfr_dvfs_notice_wq = create_singlethread_workqueue("dfr_dvfs_notice_wq");
	if (!dfr_ctrl->dfr_dvfs_notice_wq) {
		dpu_pr_err("dfr dvfs work queue create failed");
		return;
	}

	INIT_WORK(&dfr_ctrl->dfr_dvfs_notice_work, dpu_dfr_dvfs_notice_handle_func);
}

void dpu_dfr_dvfs_notice_unregister(struct dpu_comp_dfr_ctrl *dfr_ctrl)
{
	if ((dfr_ctrl->mode != DFR_MODE_LONG_H && dfr_ctrl->mode != DFR_MODE_LONG_VH &&
			dfr_ctrl->mode != DFR_MODE_LONGH_TE_SKIP_BY_MCU && 
			dfr_ctrl->mode != DFR_MODE_LONGH_BY_MCU) || !dfr_ctrl->dfr_dvfs_notice_wq)
		return;

	destroy_workqueue(dfr_ctrl->dfr_dvfs_notice_wq);
	dfr_ctrl->dfr_dvfs_notice_wq = NULL;
}