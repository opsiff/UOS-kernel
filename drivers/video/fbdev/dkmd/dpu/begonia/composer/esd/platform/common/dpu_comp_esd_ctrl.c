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

#include "dpu_comp_esd.h"
#include "dkmd_log.h"
#include "dpu_comp_mgr.h"
#include "dpu_connector.h"
#include "peri/dkmd_connector.h"
#include "dpu_comp_online.h"
#include "panel_mgr.h"
#include "dpu_comp_vsync.h"
#include "dp_drv.h"
#include "dksm_utils.h"

static int dpu_comp_dp_esd_handle(struct dp_ctrl *dptx)
{
	int ret = 0;

	if (dptx->esd_debug_mode == 1) {
		dpu_pr_debug("dp_debug_get_esd_mode esd_handle!\n");
		if (dptx->esd_debug_trigger == 1)
			ret = -1;
	} else if (dptx->dp_panel_mgr->esd_handle) {
		ret = dptx->dp_panel_mgr->esd_handle();
		dpu_pr_debug("dp_panel_mgr->esd_handle %d!\n", ret);
	}

	return ret;
}

int dpu_comp_ctrl_esd(struct dpu_composer *dpu_comp, struct comp_online_present *present)
{
	int ret = 0;
	struct dpu_panel_ops *pops = NULL;
	struct dp_private *dp_priv = NULL;
	struct dp_ctrl *dptx = NULL;
	struct dpu_connector * connector = NULL;
	struct composer_manager *comp_mgr = NULL;

	/* skip esd check, goto esd_recovery */
	if (g_enable_esd_recovery != 0)
		return 1;

	dpu_check_and_return(!dpu_comp, 0, err, "dpu_comp is NULL\n");
	comp_mgr = dpu_comp->comp_mgr;
	dpu_comp_status_debug(&comp_mgr->power_status);
	connector = get_primary_connector(dpu_comp->conn_info);
	dpu_check_and_return(!connector, 0, err, "connector is NULL\n");

	down(&comp_mgr->power_sem);
	if (!composer_check_power_status(dpu_comp)) {
		dpu_pr_warn("composer %d, panel power off!\n", dpu_comp->comp.index);
		up(&comp_mgr->power_sem);
		return ret;
	}

	dpu_pr_debug("dpu_comp->comp.index %d, dpu_comp->conn_info->base.id %d",
		dpu_comp->comp.index, dpu_comp->conn_info->base.id);

	if (is_dp_primary_panel(&dpu_comp->comp.base)) {
		dp_priv = to_dp_private(connector->conn_info);
		if (!dp_priv) {
			dpu_pr_err("dp_priv is null\n");
			up(&comp_mgr->power_sem);
			return 0;
		}
		dptx = &dp_priv->dp[MASTER_DPTX_IDX];
		if (!dptx || !dptx->dp_panel_mgr) {
			dpu_pr_warn("dp_panel_mgr is null\n");
			up(&comp_mgr->power_sem);
			return ret;
		}
	} else {
		pops = get_panel_ops(dpu_comp->conn_info->base.id);
		if (!pops || !pops->esd_handle) {
			dpu_pr_warn("esd_handle is null\n");
			up(&comp_mgr->power_sem);
			return ret;
		}
	}

	dpu_comp_active_vsync(dpu_comp);
	atomic_set(&dpu_comp->esd_ctrl.esd_check_is_doing, 1);
	if (present->dfr_ctrl.cur_frm_rate != present->dfr_ctrl.pre_frm_rate &&
		present->dfr_ctrl.cur_frm_rate != present->dfr_ctrl.active_frm_rate) {
		dpu_pr_warn("can not do esd check when frame rate is changing\n");
		goto err_out;
	}

	if (is_ppc_support(&(dpu_comp->conn_info->base)) &&
		(dpu_comp->conn_info->ppc_config_id_record != dpu_comp->conn_info->ppc_config_id_active)) {
		dpu_pr_warn("can not do esd check when glmode is changing");
		goto err_out;
	}

	/* wait when mipi_resource is available */
	if (wait_flag_in_range(&connector->post_info[connector->active_idx]->mipi.dsi_bit_clk_upt_flag, 0, 1000, ESD_WAIT_MIPI_AVAILABLE_TIMEOUT) != 0) {
		dpu_pr_warn("wait mipi_available timeout");
		goto err_out;
	}

	/* wait for vsync coming */
	if (wait_flag_in_range(&dpu_comp->esd_ctrl.is_vsync_comming, 1, 1000, ESD_WAIT_VSYNC_TIME_COUNT) == 0)
		usleep_range(ESD_CHECK_AFTER_VSYNC_TIME_PERIOD, ESD_CHECK_AFTER_VSYNC_TIME_PERIOD + 100);
	else
		dpu_pr_warn("wait vsync timeout");

	dpu_esd_timing_ctrl(dpu_comp, present);

	if (is_dp_primary_panel(&dpu_comp->comp.base)) {
		ret = dpu_comp_dp_esd_handle(dptx);
	} else if (pops->esd_handle) {
		ret = pops->esd_handle();
		dpu_pr_debug("pops->esd_handle %d!\n", ret);
	}

err_out:
	dpu_comp_deactive_vsync(dpu_comp);
	atomic_set(&dpu_comp->esd_ctrl.esd_check_is_doing, 0);
	up(&comp_mgr->power_sem);

	return ret;
}