/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.
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

#include "dp_ctrl.h"
#include "dpu_conn_mgr.h"
#include "dp_drv.h"
#include "dp_aux.h"
#include "dp_ctrl_config.h"
#include "dp_maintenance.h"
#include <platform_include/display/linux/dpu_dss_dp.h>
#include <platform_include/basicplatform/linux/dfx_bbox_diaginfo.h>
#include "drm_dp_helper_additions.h"
#include "dpu_connector.h"
#include "dp_hdmi_common.h"
#include "dksm_utils.h"
#include "dpu_dp_dbg.h"
#include <securec.h>
#include "hidptx_dp_avgen.h"
#include "utils/comm_utils.h"

void dp_maintenance_add_error_log(struct dp_ctrl *dptx, const char *error_str)
{
	size_t error_str_length;
	dpu_check_and_no_retval(!dptx || !error_str, err, "[DP] NULL Pointer\n");
	error_str_length = strlen(error_str);
	if (error_str_length >= ERROR_lOG_SIZE) {
		dpu_pr_info("[DP] str too long compared to max size\n");
		error_str_length = ERROR_lOG_SIZE;
	}

	mutex_lock(&dptx->dptx_dmd_mutex);
	if (memcpy_s(dptx->err_logs_stc.error_logs[dptx->err_logs_stc.error_current_point],
			ERROR_lOG_SIZE, error_str, error_str_length) != EOK) {
		dpu_pr_err("[DP] memcpy error_logs buffer error!");
		mutex_unlock(&dptx->dptx_dmd_mutex);
		return;
	}
	dptx->err_logs_stc.error_logs[dptx->err_logs_stc.error_current_point][error_str_length] = '\0';

	dptx->err_logs_stc.error_count++;
	dptx->err_logs_stc.error_current_point++;
	dptx->err_logs_stc.error_current_point = dptx->err_logs_stc.error_current_point % ERROR_lOG_ROW;

	if (dptx->err_logs_stc.error_count > ERROR_lOG_ROW) {
		dptx->err_logs_stc.error_count = ERROR_lOG_ROW;
		dptx->err_logs_stc.error_start_point++;
		dptx->err_logs_stc.error_start_point = dptx->err_logs_stc.error_start_point % ERROR_lOG_ROW;
	}
	mutex_unlock(&dptx->dptx_dmd_mutex);
}

void dp_maintenance_reset_error_log(struct dp_ctrl *dptx)
{
	mutex_lock(&dptx->dptx_dmd_mutex);
	dptx->err_logs_stc.error_count = 0;
	dptx->err_logs_stc.error_current_point = 0;
	dptx->err_logs_stc.error_start_point = 0;
	mutex_unlock(&dptx->dptx_dmd_mutex);
}

static void dp_bbox_upload_record(struct dp_ctrl *dptx, int dfx_type, uint64_t current_ms, int error_type)
{
	uint64_t connect_jiffies = 0;
	uint32_t i = 0;
	char tmpstr[ERROR_TMP_SIZE + 1] = {0};

	(void)memset_s(dptx->poststr, DMD_STR_LEN, 0, DMD_STR_LEN);
	(void)memset_s(dptx->logstr, DMD_ERR_LEN, 0, DMD_ERR_LEN);

	if (dfx_type != DMD_DP_FAILED) {
		if (dfx_type == DMD_DP_DISCONNECT && dptx->plugin_jiffies > 0 && dptx->plugin_jiffies < jiffies)
			dptx->holdon_time += (jiffies - dptx->plugin_jiffies) / HZ;
		connect_jiffies = dptx->holdon_time;
		dptx->holdon_time = 0;
	} else {
		for (i = 0; i < dptx->err_logs_stc.error_count; i++) {
			if (sprintf_s(tmpstr, ERROR_TMP_SIZE, "%s|", dptx->err_logs_stc.error_logs[(
					dptx->err_logs_stc.error_start_point + i) % ERROR_lOG_ROW]) <= 0) {
				dpu_pr_err("[DP] dmd tmp create failed");
				return;
			}
			dpu_pr_info("[DP] DMD_DP_DFX: tmpstr str : %s, cnt: %d", tmpstr, dptx->err_logs_stc.error_count);
			if (strcat_s(dptx->logstr, DMD_ERR_LEN, tmpstr) != 0) {
				dpu_pr_err("[DP] dmd logstr create failed, tmpstr:%s, logstr:%s,", tmpstr, dptx->logstr);
				return;
			}
		}
		dpu_pr_info("[DP] DMD_DP_DFX: logstr str : %s, cnt: %u", dptx->logstr, dptx->err_logs_stc.error_count);
		if (strlen(dptx->logstr) > 0 && strlen(dptx->logstr) < DMD_ERR_LEN)
			dptx->logstr[strlen(dptx->logstr) - 1] = '\0';
	}

	if (sprintf_s(dptx->poststr, DMD_STR_LEN, "portid:%d;dfx_type:%d;error:%d;mode:%llu;in_times:%u;out_times:%u;\
hold_on:%lu;rate=%u;lanes=%u;xres:%d;yres:%d;hbp=%d;hfp:%d;hpw:%d;vbp:%d;vfp:%d;vpw:%d;hplr:%hhu;vplr:%hhu;pcr_div=%d;\
pc_rate:%llu;fps:%hhu;dsc:%d;fec:%d;dpcd_rev:%x;delay:%d;logstr:%s;",
			dptx->port_id, dfx_type, error_type, dptx->mode, dptx->dmd_connect_cnt, dptx->dmd_disconnect_cnt,
			connect_jiffies, dptx->link.rate, dptx->link.lanes,
			dptx->connector->conn_info->base.xres, dptx->connector->conn_info->base.yres,
			dptx->connector->ldi.h_back_porch, dptx->connector->ldi.h_front_porch, dptx->connector->ldi.h_pulse_width,
			dptx->connector->ldi.v_back_porch, dptx->connector->ldi.v_front_porch, dptx->connector->ldi.v_pulse_width,
			dptx->connector->ldi.hsync_plr, dptx->connector->ldi.vsync_plr, dptx->connector->ldi.pxl_clk_rate_div,
			dptx->connector->ldi.pxl_clk_rate, dptx->vparams.m_fps, dptx->dsc, dptx->dptx_link_params.compress_params.fec,
			dptx->dpcd_rev, g_dp_dmd_delay_time, dptx->logstr) <= 0) {
		dpu_pr_err("[DP] dmd poststr create failed");
		return;
	}

	dpu_pr_info("[DP] DMD_DP_DFX: dmd str : %s", dptx->poststr);
	if (dfx_type != DMD_DP_FAILED) {
		if (connect_jiffies > 0)
			bbox_diaginfo_record(DMD_SOC_DSS_DP_INFO, NULL, dptx->poststr);
		dptx->dmd_disconnect_cnt = 0;
		dptx->dmd_connect_cnt = 0;
		dptx->update_dmd_time = current_ms;
        dpu_pr_info("[DP] set update_dmd_time: %lu", dptx->update_dmd_time);
		return;
	}
	bbox_diaginfo_record(DMD_SOC_DSS_DP_ERROR, NULL, dptx->poststr);
}

void dp_maintenance_dmd_print_dfx(struct dp_ctrl *dptx, int dfx_type, int error_type)
{
	uint64_t current_ms = 0;
	current_ms = (uint64_t)ktime_to_ms(ktime_get());

	if (dfx_type == DMD_DP_CONNECT && dptx->error_type == DMD_DP_FAILED_SAFEMODE)
		dfx_type = DMD_DP_FAILED;
	else if (dfx_type == DMD_DP_FAILED && dptx->error_type == DMD_DP_SUCCESS)
		error_type = DMD_DP_FAILED_COMMON;

	mutex_lock(&dptx->dptx_dmd_mutex);
	if (dptx->update_dmd_time == 0) {
		dptx->update_dmd_time = current_ms;
        dpu_pr_info("[DP] set update_dmd_time: %lu", dptx->update_dmd_time);
    }

	dpu_pr_info("[DP] DMD_DP_DFX:[%d-%d] dptx->update_dmd_time: %lu, current_ms: %lu, delay: %d",
		dfx_type, error_type, dptx->update_dmd_time, current_ms, g_dp_dmd_delay_time);

	if (dfx_type == DMD_DP_CONNECT) {
		dptx->dmd_connect_cnt++;
		if (current_ms - dptx->update_dmd_time < g_dp_dmd_delay_time) {
			mutex_unlock(&dptx->dptx_dmd_mutex);
			return;
		}
	} else if (dfx_type == DMD_DP_DISCONNECT) {
		dptx->dmd_disconnect_cnt++;
		if (current_ms - dptx->update_dmd_time < g_dp_dmd_delay_time) {
			if(dptx->plugin_jiffies > 0 && jiffies > dptx->plugin_jiffies)
				dptx->holdon_time += (jiffies - dptx->plugin_jiffies) / HZ;
			mutex_unlock(&dptx->dptx_dmd_mutex);
			return;
		}
	}

	// upload dmd report.
	dp_bbox_upload_record(dptx, dfx_type, current_ms, error_type);

	// reset had lock of dmd_mutex, so unlock first.
	mutex_unlock(&dptx->dptx_dmd_mutex);
	dp_maintenance_reset_error_log(dptx);
}

static bool dptx_lane_status_valid(struct dp_ctrl *dptx, uint8_t lane_index)
{
	uint8_t lane_num = dptx->link.lanes;
	const uint8_t lane_num_max = 4;

	if (lane_num == 1)
		return lane_index == 0;
	else if (lane_num == 2)
		return (lane_index == 0 || lane_index == 1);
	else if (lane_num == lane_num_max)
		return true;
	return false;
}

static bool dptx_lane_has_symbol_error(struct dp_ctrl *dptx, uint8_t lane_index, uint8_t symbol_error_count_l,
	uint8_t symbol_error_count_h)
{
	uint16_t lane_symbol_err_count = 0;
	const uint8_t symbol_error_count_mask = 0x7f;
	const uint8_t flag_mask = 7;

	dpu_pr_debug("[DP] port id %d symbol_error_count_l is %x, symbol_error_count_h is %x", dptx->port_id,
		symbol_error_count_l, symbol_error_count_h);
	// The last one indicates whether the symbol error count is valid.
	if ((symbol_error_count_h & BIT(flag_mask)) == 0) {
		dpu_pr_info("[DP] Read symbol error count isn't valid");
		return false;
	}

	lane_symbol_err_count = (((symbol_error_count_h & symbol_error_count_mask) << 8) | symbol_error_count_l);
	if (lane_symbol_err_count != 0) {
		dpu_pr_warn("[DP] port id %d,lane index is %u, symbol error count is %x", dptx->port_id, lane_index,
			lane_symbol_err_count);
		return true;
	}
	return false;
}

static bool dptx_check_symbol_err(struct dp_ctrl *dptx)
{
	const uint8_t lane_num_max = 4;
	const uint8_t symbol_error_count_len = lane_num_max * 2;
	uint8_t all_lane_symbol_error[symbol_error_count_len] = {0};
	uint8_t lane_index = 0;
	bool has_symbol_error = false;

	if (dptx_read_bytes_from_dpcd(dptx, DP_SYMBOL_ERROR_COUNT_LANE0, all_lane_symbol_error,
		sizeof(all_lane_symbol_error)) != 0) {
		dpu_pr_warn("[DP] Read DPCD error");
		return true;
	}

	for (lane_index = 0; lane_index < lane_num_max; lane_index++) {
		if (!dptx_lane_status_valid(dptx, lane_index)) {
			dpu_pr_debug("lane_index %u status is invalid, lane num is %u", lane_index, dptx->link.lanes);
			continue;
		}

		if (dptx_lane_has_symbol_error(dptx, lane_index, all_lane_symbol_error[lane_index * 2],
			all_lane_symbol_error[lane_index * 2 + 1]))
			has_symbol_error = true;
	}

	if (g_dp_debug_symbol_error_count >= 0 && (g_dp_debug_symbol_error_count & BIT(dptx->port_id)) != 0) {
		dpu_pr_warn("[DP] debug error detection port id:%d", dptx->port_id);
		return true;
	}
	return has_symbol_error;
}

void dptx_symbol_error_count_check_handler(struct work_struct *work)
{
	struct dp_ctrl *dptx = NULL;
	bool has_symbol_err = false;
	const uint8_t detect_time_max = 4;
	dptx = container_of(work, struct dp_ctrl, dptx_maintenance_check_ber_work);
	dpu_check_and_no_retval((dptx == NULL), err, "[DPTX] dptx is NULL!");

	if (!dptx_get_enable_status(dptx)) {
		dpu_pr_warn("[DP] port id %d is not enable!", dptx->port_id);
		return;
	}
	if (!dptx->is_ldi_enable) {
		dpu_pr_warn("[DP] port id %d is not enable ldi!", dptx->port_id);
		return;
	}
	mutex_lock(&dptx->dptx_mutex);
	if (!dptx_get_enable_status(dptx)) {
		mutex_unlock(&dptx->dptx_mutex);
		return;
	}

	dpu_pr_debug("[DP] dptx_symbol_error_count_check_wq_handler port id %d!", dptx->port_id);
	has_symbol_err = dptx_check_symbol_err(dptx);

	/*
	 * dptx need check err count when video has been transmited on wallex.
	 * The first checking result should be discarded, and then, we need report
	 * the bad message when the err count has been detected by 3 times in
	 * a row. The time interval of detecting is 2 second.
	 */
	dptx->detect_times = has_symbol_err ? dptx->detect_times + 1 : 1;
	if (dptx->detect_times >= detect_time_max) {
		dptx->detect_times = 1;
		switch_notification_event_unchange_state(&dptx->sdev, HOT_LINK_SYMBOL_ERROR);
		dpu_pr_info("[DP] err count upload! port id is %d", dptx->port_id);
	}
	mutex_unlock(&dptx->dptx_mutex);
}

static enum hrtimer_restart dp_maintenance_detect_hrtimer_fnc(struct hrtimer *timer)
{
	struct dp_ctrl *dptx = NULL;
	dpu_check_and_return(!timer, HRTIMER_NORESTART, err, "[DP] timer is NULL!");
	dptx = container_of(timer, struct dp_ctrl, dptx_maintenance_hrtimer);
	dpu_check_and_return(!dptx, HRTIMER_NORESTART, err, "[DP] dptx is NULL!");
	dpu_pr_debug("[DP] dp_maintenance_detect_hrtimer_fnc, port id=%d", dptx->port_id);
	dptx_work_queue_handle(dptx, DPTX_MAINTENANCE_CHECK_SYMBOL_ERROR);
	hrtimer_start(&dptx->dptx_maintenance_hrtimer, ktime_set(2, 0), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}


void dp_maintenance_init_detect_work(struct dp_ctrl *dptx)
{
	dpu_check_and_no_retval(!dptx, err, "[DP] dptx is NULL!");
	if (!dptx_is_usb_dp_panel(dptx->port_id))
		return;

	mutex_lock(&dptx->dptx_maintenance_wq_mutex);
	if (!dptx->dptx_maintenance_detect_inited) {
		dpu_pr_info("[DP] Init Detect work, port id=%d", dptx->port_id);
		/* hrtimer for detecting error count */
		hrtimer_init(&dptx->dptx_maintenance_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		dptx->dptx_maintenance_hrtimer.function = dp_maintenance_detect_hrtimer_fnc;
		hrtimer_start(&dptx->dptx_maintenance_hrtimer, ktime_set(2, 0), HRTIMER_MODE_REL);

		dptx->dptx_maintenance_detect_inited = true;
		dptx->detect_times = 0;
	}
	mutex_unlock(&dptx->dptx_maintenance_wq_mutex);
}

void dp_maintenance_deinit_detect_work(struct dp_ctrl *dptx)
{
	dpu_check_and_no_retval(!dptx, err, "[DP] dptx is NULL!");
	mutex_lock(&dptx->dptx_maintenance_wq_mutex);
	if (dptx->dptx_maintenance_detect_inited) {
		dpu_pr_info("[DP] deinit Detect work, port id=%d", dptx->port_id);
		hrtimer_cancel(&dptx->dptx_maintenance_hrtimer);

		dptx->dptx_maintenance_detect_inited = false;
		dptx->detect_times = 0;
	}
	mutex_unlock(&dptx->dptx_maintenance_wq_mutex);
}
