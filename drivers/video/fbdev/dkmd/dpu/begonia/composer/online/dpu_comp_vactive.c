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

#include <linux/delay.h>

#include "ukmd_listener.h"
#include "dpu_comp_mgr.h"
#include "dpu_comp_vactive.h"
#include "panel_mgr.h"
#include "dpu_conn_mgr.h"
#include "mipi_dsi_dev.h"
#include "dpu_comp_config_utils.h"
#include "mipi_dsi_bit_clk_upt_helper.h"
#include "dpu_comp_abnormal_handle.h"
#include "dksm_dmd.h"
#include "dksm_debug.h"
#include "dkmd_mntn_rdr.h"
#include "dpu_mntn.h"
#include "dkmd_lcd_interface.h"

#define SEC_IN_US 1000000
#define FRAME_RATE_30 30
#define VSTART_TIMEOUT_RETRY_COUNT 6
#define VSTART_SCHEDULE_TIMEOUT_THRESHOLD 2

static bool g_dpu_underflow_itr = false;

static bool dpu_comp_longvh_ctrl_in_vactive_end(struct dpu_composer *dpu_comp)
{
	struct dkmd_connector_info *pinfo = NULL;
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = &present->dfr_ctrl;

	dpu_check_and_return(!dpu_comp, false, err, "dpu_comp is null\n");
	pinfo = dpu_comp->conn_info;
	dpu_check_and_return(!pinfo, false, err, "pinfo is null\n");
	if(dfr_ctrl->pre_frm_rate < dfr_ctrl->cur_frm_rate && dfr_ctrl->pre_frm_rate == FRAME_RATE_30){
		return true;
	}

	return pinfo->longvh_vactive_end_ctrl_support;
}

static void dpu_comp_update_frm_rate_longvh(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct dpu_composer *dpu_comp)
{
	struct dpu_connector *connector = NULL;
	if (dfr_ctrl->mode != DFR_MODE_LONG_VH)
		return;
 
	// if ture , should update frame in vactive_end
	if (dpu_comp_longvh_ctrl_in_vactive_end(dpu_comp))
		return;
	connector = get_primary_connector(dpu_comp->conn_info);
	// if ture , may update frame rate in vactive_end and cause underflow .
	if (connector->is_vactive_start_missed){
		dpu_pr_warn("vactive_start and vactive_end received at the same time, cannot change update frm");
		return;
	}
	if (dfr_ctrl->ops && dfr_ctrl->ops->update_frm_rate_isr_handler)
		dfr_ctrl->ops->update_frm_rate_isr_handler(dfr_ctrl);
}

static int32_t dpu_comp_vactive_isr_notify(struct notifier_block *self, unsigned long action, void *data)
{
	struct ukmd_listener_data *listener_data = NULL;
	struct dpu_composer *dpu_comp = NULL;
	struct comp_online_present *present = NULL;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = NULL;
	struct dpu_multi_present_ctrl *multi_present_ctrl = NULL;
	bool main_client_present_flag = true;
	bool enable_multi_present = false;

	if (!data) {
		dpu_pr_err("data is null pointer\n");
		return -1;
	}

	listener_data = (struct ukmd_listener_data *)data;
	dpu_comp = (struct dpu_composer *)(listener_data->data);
	present = (struct comp_online_present *)dpu_comp->present_data;
	dfr_ctrl = &present->dfr_ctrl;

	multi_present_ctrl = &dpu_comp->multi_present_ctrl;
	// only cmd need get multi present param
	enable_multi_present = ((is_mipi_cmd_panel(&dpu_comp->conn_info->base) || is_edp_cmd_mode(dpu_comp->conn_info)) &&
		(multi_present_ctrl->ops &&
		multi_present_ctrl->ops->clear_sfu2vactive_param && multi_present_ctrl->ops->in_sfu2vactive_period));
	if (enable_multi_present) {
		main_client_present_flag = multi_present_ctrl->ops->in_sfu2vactive_period(dfr_ctrl, PRESENT_CLIENT_MAIN);
		multi_present_ctrl->ops->clear_sfu2vactive_param(dfr_ctrl);
	}

	dpu_comp->comp.hw_present_frame_index = dpu_comp->comp.sw_present_frame_index;

	if (main_client_present_flag)
		++present->vactive_start_flag;
	present->vactive_start_timestamp = ktime_get();

	if (dpu_comp->bl_ctrl.is_inited == true && dpu_comp->bl_ctrl.bl_updated == 0)
		kthread_queue_work(&dpu_comp->handle_worker, &dpu_comp->bl_ctrl.bl_work);

	dpu_comp_update_frm_rate_longvh(dfr_ctrl, dpu_comp);

	if (present->buffers > 0 && main_client_present_flag) {
		present->buffers--;
		dpu_pr_debug("unpresent buffer num is %d", present->buffers);
#ifdef CONFIG_DKMD_DEBUG_ENABLE
		trace_buffer_num(present->buffers);
#endif
	}

	if (is_mipi_cmd_panel(&dpu_comp->conn_info->base) && dpu_comp->conn_info->disable_ldi) {
		present->vactive_end_flag = 0;
		// Closing ldi in the tui scene will cause the tui layer freeze.
		if (likely(dpu_tui_no_need_disable_ldi(dpu_comp))) {
			/* disable mipi ldi */
			dpu_comp->conn_info->disable_ldi(dpu_comp->conn_info);
			dpu_pr_debug("vactive start disable ldi!");
		}
		dpu_pr_debug("panel id is %d, compose index = %d",
			dpu_comp->conn_info->base.id, dpu_comp->comp.index);
		if (dpu_comp->conn_info->check_ldi_status) {
			present->vactive_ldi_status = dpu_comp->conn_info->check_ldi_status(dpu_comp->conn_info);
			if (present->vactive_ldi_status != 0) {
				dpu_comp->comp.comp_err_status |= ABNORMAL_CHECK_LDI_FAILED;
				kthread_queue_work(&dpu_comp->handle_worker, &present->abnormal_handle_work);
			}
		}
	}

	dpu_check_dbuf_state(dpu_comp->comp_mgr->dpu_base, (uint32_t)dpu_comp->comp_scene_id);

	dpu_check_tunnel_state(dpu_comp);

	if (g_debug_dvfs_auto_test_enable == 1) {
		dpu_dvfs_auto_record_freq();
		dpu_print_dvfs_vote_status();
	}
	dpu_get_cur_peri_level(dpu_comp);

	// notify lcdkit vactive start event
	pipeline_next_handle_event(dpu_comp->conn_info->conn_device, dpu_comp->conn_info,
		DPU_EVENT_ISR_VACTIVE_START, NULL, true);

	wake_up_interruptible_all(&present->vactive_start_wq);
	return 0;
}

static struct notifier_block vactive_isr_notifier = {
	.notifier_call = dpu_comp_vactive_isr_notify,
};

void dpu_comp_vactive_init(struct ukmd_isr *isr, struct dpu_composer *dpu_comp, uint32_t listening_bit)
{
	struct comp_online_present *present = NULL;
	if (!isr) {
		dpu_pr_err("isr is null pointer\n");
		return;
	}
	if (!dpu_comp) {
		dpu_pr_err("dpu_comp is null pointer\n");
		return;
	}

	present = (struct comp_online_present *)dpu_comp->present_data;

	init_waitqueue_head(&present->vactive_start_wq);
	present->vactive_start_flag = 1;

	ukmd_isr_register_listener(isr, &vactive_isr_notifier, listening_bit, dpu_comp);
}

void dpu_comp_vactive_deinit(struct ukmd_isr *isr, uint32_t listening_bit)
{
	if (!isr) {
		dpu_pr_err("pointer isr is nullptr!\n");
		return;
	}
	ukmd_isr_unregister_listener(isr, &vactive_isr_notifier, listening_bit);
}

static uint32_t get_connector_vactive_irq_status(struct comp_online_present *present)
{
	uint32_t isr_status = 0U;
	struct dkmd_connector_info *pinfo = NULL;
	struct dpu_composer *dpu_comp = present->dpu_comp;
	if (unlikely(!dpu_comp)) {
		dpu_pr_err("dpu_comp is null\n");
		return 0;
	}

	pinfo = dpu_comp->conn_info;
	if (unlikely(!pinfo)) {
		dpu_pr_err("pinfo is null\n");
		return 0;
	}

	pipeline_next_ops_handle(pinfo->conn_device, pinfo, GET_VACTIVE_IRQ_STATUS, (void *)&isr_status);

	return isr_status;
}

static int32_t wait_vactive_event_for_panel(struct comp_online_present *present, int32_t timeout)
{
	int32_t ret = 0;
	uint32_t pre_vactive_start_flag = present->vactive_start_flag;
	struct dpu_composer *dpu_comp = present->dpu_comp;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = &present->dfr_ctrl;

	if (is_mipi_cmd_panel(&dpu_comp->conn_info->base) || is_edp_cmd_mode(dpu_comp->conn_info)) {
		ret = wait_event_interruptible_timeout(present->vactive_start_wq,
			present->vactive_start_flag &&
			(!dpu_comp->multi_present_ctrl.ops || !dpu_comp->multi_present_ctrl.ops->is_safe_period ||
				dpu_comp->multi_present_ctrl.ops->is_safe_period(dfr_ctrl)),
			(long)msecs_to_jiffies((uint32_t)timeout));
	} else {
		ret = (int32_t)wait_event_interruptible_timeout(present->vactive_start_wq,
			((pre_vactive_start_flag != present->vactive_start_flag) && (present->vactive_start_flag > 0)) ||
			(present->vactive_start_flag == 1),
			(long)msecs_to_jiffies((uint32_t)timeout));
	}

	return ret;
}

static int32_t wait_vactive_with_check_irq_status(struct comp_online_present *present, int32_t timeout, uint32_t max_try_count)
{
	uint32_t scene_id = (uint32_t)present->frames[0].in_frame.scene_id;
	int32_t ret = 0;
	uint32_t try_times = 0U;
	ktime_t dss_vactive_start_time = ktime_get();
	ktime_t cpu_vactive_start_time = 0;
	bool dss_vactive_irq_raised = false;
	bool is_vactive_timeout = true;
	int64_t excess_time = 0;

	for (try_times = 0U; try_times < max_try_count; try_times++) {
		ret = wait_vactive_event_for_panel(present, timeout);
		if (ret == 0 && !dss_vactive_irq_raised && get_connector_vactive_irq_status(present)) {
			dss_vactive_irq_raised = true;
			dss_vactive_start_time = ktime_get();
		} else if (ret > 0) {
			is_vactive_timeout = false;
			cpu_vactive_start_time = ktime_get();
			if (dss_vactive_irq_raised && (cpu_vactive_start_time > dss_vactive_start_time) &&
				(ktime_ms_delta(cpu_vactive_start_time, dss_vactive_start_time) >= VSTART_SCHEDULE_TIMEOUT_THRESHOLD * timeout)) {
				excess_time = ktime_ms_delta(cpu_vactive_start_time, dss_vactive_start_time);
				dpu_pr_warn("obeserve cpu irq on scene=%u, but take more than %lldms from dss irq\n", scene_id, excess_time);
			}
			return ret;
		} else if (ret == -ERESTARTSYS){
			dpu_pr_warn("wait vactive start event was interrupted");
			return ret;
		}
	}

	if (!dss_vactive_irq_raised) {
		dpu_pr_err("cannot obeserve vactive start irq, even not in reg of irq, need reset dss hardware\n");
		return 0;
	}

	excess_time = ktime_ms_delta(ktime_get(), dss_vactive_start_time);
	dpu_pr_warn("wait vactive timeout on scene=%u, but vative start irq is raised, no need to reset dss and just stop present\n", scene_id);
	dksm_dmd_report_vactive_timeout(scene_id, is_vactive_timeout, excess_time);
	return -1;
}

static int32_t video_panel_wait_vactive(struct comp_online_present *present, int32_t timeout)
{
	uint32_t scene_id = (uint32_t)present->frames[0].in_frame.scene_id;
	uint64_t cur_ts = (uint64_t)ktime_to_us(ktime_get());
	uint64_t vsync_ts = (uint64_t)ktime_to_us(present->vsync_ctrl.timestamp);
	uint64_t prev_vsync_ts = (uint64_t)ktime_to_us(present->vsync_ctrl.prev_timestamp);
	uint64_t vactive_ts = (uint64_t)ktime_to_us(present->vactive_start_timestamp);
	int32_t ret = 0;
	uint32_t vsync_interval;
	uint32_t frame_no = present->frames[present->displaying_idx].in_frame.frame_index;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = &present->dfr_ctrl;

	if (vsync_ts == 0) {
		dpu_pr_debug("first frame, scene: %d", scene_id);
		return 1;
	}

	if (dfr_ctrl->mode != DFR_MODE_CONSTANT && dfr_ctrl->pre_frm_rate > 0)
		vsync_interval = SEC_IN_US / dfr_ctrl->pre_frm_rate;
	else
		vsync_interval = SEC_IN_US / present->dpu_comp->conn_info->base.fps;

	if (log_should_dump_data(frame_no)) {
		dpu_pr_info("cur_ts: %llu, prev_vsync_ts: %llu, vsync_ts: %llu, vactive_ts: %llu, vsync_interval: %u, scene: %d",
			cur_ts, prev_vsync_ts, vsync_ts, vactive_ts, vsync_interval, scene_id);
	}

	if (cur_ts > vactive_ts && vactive_ts > vsync_ts && prev_vsync_ts != vsync_ts &&
		cur_ts > vsync_ts && cur_ts - vsync_ts < vsync_interval - g_vsync_vactive_time_gap) {
		if (log_should_dump_data(frame_no)) {
			dpu_pr_info("immediate present");
		}
		ret = 1;
	} else {
        ret = wait_vactive_with_check_irq_status(present, timeout / VSTART_TIMEOUT_RETRY_COUNT, VSTART_TIMEOUT_RETRY_COUNT);
		dpu_pr_info("wait next vactive present, time diff: %llu", ktime_to_us(ktime_get()) - cur_ts);
	}

	return ret;
}

static int32_t dpu_comp_vactive_wait(struct comp_online_present *present, struct dpu_composer *dpu_comp, int32_t timeout)
{
	int32_t ret = 0;
	int times = 0;

	while (true) {
		if (is_mipi_cmd_panel(&dpu_comp->conn_info->base) || is_edp_cmd_mode(dpu_comp->conn_info)) {
			ret = wait_vactive_with_check_irq_status(present, timeout / VSTART_TIMEOUT_RETRY_COUNT, VSTART_TIMEOUT_RETRY_COUNT);
		} else {
			ret = video_panel_wait_vactive(present, timeout);
		}

		if ((ret == -ERESTARTSYS) && (times++ < 50))
			mdelay(10);
		else
			break;
	}

	return ret;
}

static void dpu_comp_rdr_dump_info(struct dpu_connector *connector, struct dpu_composer *dpu_comp)
{
	if ((dpu_comp->comp.comp_err_status & ABNORMAL_TE_IRQ_REGISTER_FAILED) != 0) {
		dpu_comp->comp.comp_err_status &= ~ABNORMAL_TE_IRQ_REGISTER_FAILED;
		return;
	}

	if ((connector->need_check_mipi_connected == false) &&
		(dpu_comp->comp_mgr->ignore_first_frame_timeout == 0))
		dkmd_rdr_dump_exception_info(MODID_DSS_VACT_IRQ_TIMEOUT, 0, 0);
}

int32_t dpu_comp_vactive_wait_event(struct comp_online_present *present)
{
	int32_t ret = 0;
	int32_t timeout = g_debug_wait_vactive_timeout_ms;
	uint64_t vactive_tv;
	uint32_t vstate;
	struct dpu_composer *dpu_comp = present->dpu_comp;
	struct dpu_connector *connector = NULL;
	int32_t lcd_status = 0;
	uint32_t phy_status = 0;

#ifdef WAIT_VACTIVE_TIMEOUT_MS
	timeout = WAIT_VACTIVE_TIMEOUT_MS;
#endif

	connector = get_primary_connector(dpu_comp->conn_info);
	dpu_trace_ts_begin(&vactive_tv);
	if (dpu_comp->conn_info->base.fpga_flag)
		timeout = FPGA_EVENT_TIMEOUT_MS;

	present = (struct comp_online_present *)dpu_comp->present_data;
	ret = dpu_comp_vactive_wait(present, dpu_comp, timeout);
	if (ret > 0) {
		if (is_mipi_cmd_panel(&dpu_comp->conn_info->base) || is_edp_cmd_mode(dpu_comp->conn_info))
			present->vactive_start_flag = 0;

		g_dpu_underflow_itr = true;
		dpu_trace_ts_timeout_end(&vactive_tv, (USEC_PER_SEC / present->frame_rate), "online compose wait vactive");
		return 0;
	} else if (ret == -1) { // failed to wait vactive_start, but not a dss problem
		dpu_trace_ts_timeout_end(&vactive_tv, (USEC_PER_SEC / present->frame_rate), "online compose wait vactive");
		dpu_comp_rdr_dump_info(connector, dpu_comp);
		return -1;
	}

	dpu_comp_rdr_dump_info(connector, dpu_comp);

	g_dpu_underflow_itr = false;
	dpu_pr_warn("pipe_sw_itfch_idx=%d scene_id %d, online compose wait vactive_start timeout!"
		"frame_rate=%u, dpp_dbg=0x%x, vcount=0x%x, ycnt=0x%x, vstate=0x%x, phy_status=0x%x, isr=0x%x",
		dpu_comp->conn_info->base.pipe_sw_itfch_idx, dpu_comp->comp_scene_id, present->frame_rate,
		inp32(DPU_DPP_CH_DBG_CNT_ADDR(dpu_comp->comp_mgr->dpu_base + DPU_DPP0_OFFSET)),
		inp32(DPU_DSI_VCOUNT_ADDR(connector->connector_base)),
		inp32(DPU_GLB_DBG_OV_YCNT0_ADDR(connector->connector_base + DPU_GLB0_OFFSET)),
		inp32(DPU_DSI_VSTATE_ADDR(connector->connector_base)),
		inp32(DPU_DSI_CDPHY_STATUS_ADDR(connector->connector_base)),
		inp32(DPU_DSI_CPU_ITF_INTS_ADDR(connector->connector_base)));

	if (dpu_comp->multi_present_ctrl.ops && dpu_comp->multi_present_ctrl.ops->dump_info)
		dpu_comp->multi_present_ctrl.ops->dump_info(&present->dfr_ctrl);

	vstate = inp32(DPU_DSI_VSTATE_ADDR(connector->connector_base));
	phy_status = inp32(DPU_DSI_CDPHY_STATUS_ADDR(connector->connector_base));

	dpu_comp_abnormal_dump_axi(dpu_comp->comp_mgr->dpu_base);

	if (dpu_comp->comp_mgr->media1_ctrl_base)
		dpu_comp_abnormal_dump_qic(dpu_comp->comp_mgr->media1_ctrl_base);
	else
		dpu_pr_warn("no media1 ctrl base\n");

	if (g_debug_present_hold) {
		dpu_pr_warn("composer present hold start");
		mdelay(DPU_COMPOSER_HOLD_TIME);
		dpu_pr_warn("composer present hold end");
	}

    if (is_mipi_cmd_panel(&dpu_comp->conn_info->base) && !is_fake_panel(&dpu_comp->conn_info->base))
	    lcd_status = dpu_comp->conn_info->lcd_status_err;

	if (dpu_comp->conn_info->esd_enable) {
		if (vstate == LDI_VSTATE_V_WAIT_TE0 || ((lcd_status != 0) && ((phy_status & BIT(1)) != 0))) {
			dpu_comp->esd_ctrl.esd_recover_state = ESD_RECOVER_STATE_START;
			if (dpu_comp->esd_ctrl.esd_check_wq)
				queue_work(dpu_comp->esd_ctrl.esd_check_wq, &(dpu_comp->esd_ctrl.esd_check_work));
		}
	}

	dpu_trace_ts_timeout_end(&vactive_tv, (USEC_PER_SEC / present->frame_rate), "online compose wait vactive");
	return (ret > 0) ? 0 : COMP_VACTIVE_TIMEOUT_RET_VALUE;
}

static int32_t dpu_comp_vactive_end_isr_notify(struct notifier_block *self, unsigned long action, void *data)
{
	struct ukmd_listener_data *listener_data = (struct ukmd_listener_data *)data;
	struct dpu_composer *dpu_comp = (struct dpu_composer *)(listener_data->data);
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_comp_dfr_ctrl *dfr_ctrl = &present->dfr_ctrl;
	struct dpu_connector * connector = get_primary_connector(dpu_comp->conn_info);
	uint32_t displayed_scene_id = 0;

	if ((dfr_ctrl->mode != DFR_MODE_LONG_VH || dpu_comp_longvh_ctrl_in_vactive_end(dpu_comp)) &&
		dfr_ctrl->ops && dfr_ctrl->ops->update_frm_rate_isr_handler)
		dfr_ctrl->ops->update_frm_rate_isr_handler(dfr_ctrl);

	if (connector->active_idx < MAX_CONN_POST_INFO_NUM) {
		if ((connector->post_info[connector->active_idx] != NULL &&
			connector->post_info[connector->active_idx]->mipi.dsi_bit_clk_upt_support != 0) ||
			(g_debug_mipi_dsi_clk_upt != 0))
			mipi_dsi_bit_clk_upt_isr_handler(connector);
	}

	if (g_debug_dump_reg && (present->displayed_idx < COMP_FRAME_MAX)) { /* use to test abnormal_debug_dump */
		displayed_scene_id = (uint32_t)(present->frames[present->displayed_idx].in_frame.scene_id);
		dpu_comp_abnormal_debug_dump(dpu_comp, displayed_scene_id);
	}

	present->vactive_end_flag = 1;
	wake_up_interruptible_all(&present->vactive_end_wq);

#ifdef CONFIG_DKMD_DEBUG_ENABLE
	dpu_comp_reset_hardware_manually(dpu_comp, present);
	dpu_comp_esd_recovery_manually(dpu_comp);
#endif
	return 0;
}

static struct notifier_block vactive_end_isr_notifier = {
	.notifier_call = dpu_comp_vactive_end_isr_notify,
};

void dpu_comp_vactive_end_init(struct ukmd_isr *isr, struct dpu_composer *dpu_comp, uint32_t listening_bit)
{
	struct comp_online_present *present = NULL;
	if (!isr) {
		dpu_pr_err("isr is null pointer\n");
		return;
	}
	if (!dpu_comp) {
		dpu_pr_err("dpu_comp is null pointer\n");
		return;
	}

	present = (struct comp_online_present *)dpu_comp->present_data;

	init_waitqueue_head(&present->vactive_end_wq);
	present->vactive_end_flag = 0;

	ukmd_isr_register_listener(isr, &vactive_end_isr_notifier, listening_bit, dpu_comp);
}

void dpu_comp_vactive_end_deinit(struct ukmd_isr *isr, uint32_t listening_bit)
{
	ukmd_isr_unregister_listener(isr, &vactive_end_isr_notifier, listening_bit);
}

bool dpu_get_underflow_itr(void)
{
	return g_dpu_underflow_itr;
}
