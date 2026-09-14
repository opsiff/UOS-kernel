/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
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
#include "mipi_dsi_async.h"
#include <dpu/soc_dpu_define.h>
#include "mipi_dsi_dev.h"
#include "dkmd_log.h"
#include "res_mgr.h"
#include "ukmd_cmdlist.h"
#include "cmdlist_interface.h"
#include "dpu_conn_mgr.h"
#include "dkmd_comp.h"
#include "mipi_config_utils.h"
#include "dksm_debug.h"
#include "dpu_comp_mgr.h"
#include "abnormal/dpu_comp_abnormal_handle.h"

#define WAIT_CNT 3000

static inline bool mipi_dsi_async_tx_is_available_cmdlist(struct mipi_dsi_async_ctrl *async_ctrl, uint32_t apply_cmdlist_num)
{
	return (uint32_t)__builtin_popcount(async_ctrl->applied_cmdlist_group) + apply_cmdlist_num <= MIPI_ASYNC_TX_MAX_NUM;
}

static bool is_cmdlists_apply_timeout(struct mipi_dsi_async_ctrl *async_ctrl)
{
	ktime_t current_time = 0;
	int64_t applied_time = 0;
	uint32_t cmdlist_group_pos = 0;
	uint32_t applied_timeout_nums = 0;
	uint32_t applied_cmdlist_group = 0;

	dpu_pr_warn("There are 0x%X cmdlists applied", __builtin_popcount(async_ctrl->applied_cmdlist_group));
	if (__builtin_popcount(async_ctrl->applied_cmdlist_group) < TIMEOUT_CMDLIST_NUMS_OF_RECOVERY)
		return false;

	current_time = ktime_get();
	applied_cmdlist_group = async_ctrl->applied_cmdlist_group;
	while (applied_cmdlist_group != 0) {
		cmdlist_group_pos = (uint32_t)__builtin_ffs(applied_cmdlist_group) - 1;
		applied_cmdlist_group &= ~(BIT(cmdlist_group_pos));
		applied_time = ktime_to_us(current_time) - ktime_to_us(async_ctrl->cmdlists[cmdlist_group_pos].applied_timestamp);
		/* Make sure the timeout cmdlist is applied */
		if ((async_ctrl->cmdlists[cmdlist_group_pos].apply_status == APPLIED) && (applied_time >= TASK_ACK_TIMEOUT_THRES_US))
			++applied_timeout_nums;
	}

	if (applied_timeout_nums > 0)
		dpu_pr_warn("There are %u cmdlists applied timeout", applied_timeout_nums);

	if (applied_timeout_nums >= TIMEOUT_CMDLIST_NUMS_OF_RECOVERY)
		return true;

	return false;
}

static bool cmdlist_apply(struct mipi_dsi_async_ctrl *async_ctrl, uint32_t pos)
{
	unsigned long flags = 0;
	spin_lock_irqsave(&async_ctrl->cmdlist_lock, flags);

	if (async_ctrl->cmdlists[pos].apply_status == NO_APPLY) {
		async_ctrl->cmdlists[pos].apply_status = APPLING;
		spin_unlock_irqrestore(&async_ctrl->cmdlist_lock, flags);
		ukmd_cmdlist_clear_client(async_ctrl->cmdlist_dev_id, async_ctrl->cmdlist_scene_id,
			async_ctrl->cmdlists[pos].cmdlist_payload);
		return true;
	}

	spin_unlock_irqrestore(&async_ctrl->cmdlist_lock, flags);
	dpu_pr_warn("no cmdlist to apply");
	return false;
}

static void cmdlist_recycle(struct mipi_dsi_async_ctrl *async_ctrl, uint32_t pos)
{
	unsigned long flags = 0;
	spin_lock_irqsave(&async_ctrl->cmdlist_lock, flags);

	if (async_ctrl->cmdlists[pos].apply_status != NO_APPLY) {
		async_ctrl->cmdlists[pos].apply_status = NO_APPLY;
		async_ctrl->cmdlists[pos].cmds_type = DKMD_CMDS_TYPE_MAX;
		async_ctrl->applied_cmdlist_group &= ~BIT(pos);
	}

	spin_unlock_irqrestore(&async_ctrl->cmdlist_lock, flags);
}

static void mipi_dsi_async_tx_update_idle_cmdlist(struct mipi_dsi_async_ctrl *async_ctrl)
{
	uint32_t pos = 0;
	unsigned long flags = 0;
	uint32_t cmdlist_group = 0;

	spin_lock_irqsave(&async_ctrl->cmdlist_lock, flags);
	for (; pos < MIPI_ASYNC_TX_MAX_NUM; pos++) {
		if (async_ctrl->cmdlists[pos].apply_status == NO_APPLY)
			cmdlist_group |= BIT(pos);
	}
	spin_unlock_irqrestore(&async_ctrl->cmdlist_lock, flags);
	async_ctrl->idle_cmdlist_group = cmdlist_group;
}

static void mipi_dsi_async_tx_print_dacc_queue_info(struct dpu_connector *connector)
{
	dpu_pr_debug("flw_size=%u, normal_size=%u, flw_num_to_tx=%u, nor_num_to_tx=%u",
		inp32(DPU_ASYNC_TX_QUEUE_FLW_SIZE(connector->dpu_base)),
		inp32(DPU_ASYNC_TX_QUEUE_NOR_SIZE(connector->dpu_base)),
		inp32(DPU_ASYNC_TX_QUEUE_FLW_NUM_TO_TX(connector->dpu_base)),
		inp32(DPU_ASYNC_TX_QUEUE_NOR_NUM_TO_TX(connector->dpu_base)));
}

static void mipi_dsi_async_tx_print_group_cmdlist_info(struct dpu_connector *connector, uint32_t cmdlist_group)
{
	uint32_t cmdlist_group_pos = 0;
	struct mipi_dsi_async_ctrl *async_ctrl = NULL;
	if (unlikely(cmdlist_group > MIPI_ASYNC_TX_MAX_NUM_OF_GROUP)) {
		dpu_pr_warn("cmdlist group %u is invalid", cmdlist_group);
		return;
	}

	async_ctrl = &connector->dsi_async_ctrl;
	while (cmdlist_group != 0) {
		cmdlist_group_pos = (uint32_t)__builtin_ffs(cmdlist_group) - 1;
		cmdlist_group &= ~(BIT(cmdlist_group_pos));
		dpu_pr_debug("idx %u cmdlist header id = %u", cmdlist_group_pos, async_ctrl->cmdlists[cmdlist_group_pos].cmdlist_header);
	}
}

static int32_t wait_for_dmcu_ops_done(struct dpu_connector *connector)
{
	int32_t ret = 0;
	uint32_t status;
	uint32_t try_cnt = 0;
	uint32_t recall_cmdlist_group = 0;
	do {
		status = read_reg_with_retry(DPU_ASYNC_TX_OPS_STATUS_ADDR(connector->dpu_base));
		try_cnt++;
	} while ((status == TX_OPS_REQ) && (try_cnt < WAIT_CNT));

	dpu_pr_debug("wait count = %d", try_cnt);
	mipi_dsi_async_tx_print_dacc_queue_info(connector);

	switch (status) {
	case TX_OPS_DONE:
		break;
	case TX_OPS_REPLACE_TASK_DONE:
		recall_cmdlist_group = read_reg_with_retry(DPU_ASYNC_TX_RECALL_CMDLIST_GROUP_ADDR(connector->dpu_base));
		mipi_dsi_async_tx_print_group_cmdlist_info(connector, recall_cmdlist_group);
		dpu_pr_info("dmcu drop repeated applied task");
		break;
	case TX_OPS_NEW_TASK_DROP:
		dpu_pr_info("dmcu drop repeated new task");
        break;
	case TX_OPS_E_REPLACE_TASK_FAILED:
		dpu_pr_err("dmcu replace task failed");
		ret = MIPI_E_REPLACE_TASK_FAILED;
		break;
	case TX_OPS_REQ:
		dpu_pr_err("dmcu ops timeout");
		ret = MIPI_E_TIMEOUT;
		break;
	case TX_OPS_E_FIFO_FULL:
	case TX_OPS_E_TOO_MANY_TASK:
		dpu_pr_err("dmcu handle one task fail,  fifo full");
		ret = MIPI_E_TOO_MANY_TXS;
		break;
	case TX_OPS_E_INVALID_PARAMS:
		dpu_pr_err("dmcu async params is invalid");
		ret = MIPI_E_INVALID_PARAMS;
	default:
		dpu_pr_warn("dmcu handle task req unknown status = %u", status);
		ret = MIPI_E_UNKNOWN;
		break;
	}

	return ret;
}

static int32_t one_cmdlist_create(uint32_t dev_id, uint32_t scene_id,
	uint32_t *header_id, uint32_t *payload_id)
{
	int32_t ret;
	uint32_t cmdlist_header_id;
	uint32_t cmdlist_payload_id;

	dev_id = CMDLIST_DEV_ID_DPU;

	cmdlist_header_id = cmdlist_create_user_client(dev_id, scene_id, SCENE_NOP_TYPE, 0, 0);
	if (unlikely(cmdlist_header_id == 0)) {
		dpu_pr_err("scene_id=%u, create header failed", scene_id);
		return -1;
	}

	cmdlist_payload_id = cmdlist_create_user_client(dev_id, scene_id,
		REGISTER_CONFIG_TYPE, 0, PAGE_SIZE);
	if (unlikely(cmdlist_payload_id == 0)) {
		dpu_pr_err("scene_id=%u create payload failed", scene_id);
		ukmd_cmdlist_release_locked(dev_id, scene_id, cmdlist_header_id);
		return -1;
	}

	ret = cmdlist_append_client(dev_id, scene_id, cmdlist_header_id, cmdlist_payload_id);
	if (unlikely(ret != 0)) {
		dpu_pr_err("append payload failed");
		ukmd_cmdlist_release_locked(dev_id, scene_id, cmdlist_header_id);
		return -1;
	}

	*header_id = cmdlist_header_id;
	*payload_id = cmdlist_payload_id;
	return 0;
}

static uint32_t calc_cmdlist_num(uint32_t cmds_num)
{
	uint32_t cmdlist_num = cmds_num / DSI_HP_FIFO_CMDS_MAX;
	return ((cmds_num % DSI_HP_FIFO_CMDS_MAX) != 0) ? (cmdlist_num + 1) : cmdlist_num;
}

static void cmdlists_destroy(struct mipi_dsi_async_ctrl *async_ctrl)
{
	int i;
	for (i = 0; i < MIPI_ASYNC_TX_MAX_NUM; i++)
		ukmd_cmdlist_release_locked(async_ctrl->cmdlist_dev_id,
			async_ctrl->cmdlist_scene_id, async_ctrl->cmdlists[i].cmdlist_header);
}

static void mipi_dsi_async_tx_recovery(struct kthread_work *work)
{
	struct composer *comp = NULL;
	struct dpu_composer *dpu_comp = NULL;
	struct dpu_connector *connector = NULL;
	struct mipi_dsi_async_ctrl *async_ctrl = NULL;

	async_ctrl = container_of(work, struct mipi_dsi_async_ctrl, recovery_handle_work);
	if (unlikely(async_ctrl == NULL)) {
		dpu_pr_err("async_ctrl is nullptr!");
		return;
	}

	connector = container_of(async_ctrl, struct dpu_connector, dsi_async_ctrl);
	if (unlikely(connector->conn_info == NULL || connector->conn_info->base.comp_obj_info == NULL)) {
		dpu_pr_err("conn_info or comp_obj_info is nullptr!");
		return;
	}

	comp = container_of(connector->conn_info->base.comp_obj_info, struct composer, base);
	if (unlikely(comp == NULL)) {
		dpu_pr_err("comp is nullptr!");
		return;
	}

	dpu_comp = to_dpu_composer(comp);
	if (unlikely(dpu_comp == NULL)) {
		dpu_pr_err("dpu_comp is nullptr!");
		return;
	}
	dpu_pr_info("async tx recover start!");
	composer_manager_power_down(dpu_comp);
	if (unlikely(dpu_comp_status_is_disable(&dpu_comp->comp_mgr->power_status))) {
		dpu_pr_info("already power off, do not need execute recover!");
		composer_manager_power_up(dpu_comp);
		return;
	}
	composer_manager_power_restart_no_lock(dpu_comp);
	comp->comp_err_status = 0;
	composer_manager_power_up(dpu_comp);
	dpu_pr_info("async tx recover success!");
	return;
}

void mipi_dsi_async_tx_recovery_init(struct dpu_connector *connector)
{
	if (unlikely(connector == NULL)) {
		dpu_pr_err("connector is nullptr!");
		return;
	}

	dpu_pr_info("+");
	kthread_init_work(&connector->dsi_async_ctrl.recovery_handle_work, mipi_dsi_async_tx_recovery);
}

int32_t mipi_dsi_async_init(struct dpu_connector *connector)
{
	int32_t i, ret;
	dma_addr_t phy_addr = 0;
	uint32_t header_id, payload_id;
	struct mipi_dsi_async_ctrl *async_ctrl = &connector->dsi_async_ctrl;
	struct dfr_info *dfr_info = dkmd_get_dfr_info(connector->conn_info);
	if (!dfr_info) {
		dpu_pr_warn("dfr_info is nullptr");
		async_ctrl->async_status = ASYNC_UNINITIALIZED;
		return -1;
	}

	if (dfr_info->dfr_mode != DFR_MODE_TE_SKIP_BY_MCU) {
		async_ctrl->async_status = ASYNC_UNINITIALIZED;
		dpu_pr_info("dfr te skip is not by MCU, async tx is not support");
		return 0;
	}

	async_ctrl->applied_cmdlist_group = 0;
    async_ctrl->idle_cmdlist_group = MIPI_ASYNC_TX_MAX_NUM_OF_GROUP;
	async_ctrl->cmdlist_scene_id = DPU_SCENE_ONLINE_3;
	async_ctrl->cmdlist_dev_id = CMDLIST_DEV_ID_DPU;
	async_ctrl->async_status = ASYNC_STARTED;

	spin_lock_init(&async_ctrl->cmdlist_lock);
	dpu_pr_info("+");

	for (i = 0; i < MIPI_ASYNC_TX_MAX_NUM; i++) {
		ret = one_cmdlist_create(async_ctrl->cmdlist_dev_id,
			async_ctrl->cmdlist_scene_id, &header_id, &payload_id);
		if (ret) {
			dpu_pr_err("create cmdlist failed, ret = %d", ret);
			return MIPI_E_ALLOC_FAILED;
		}

		phy_addr = cmdlist_get_phy_addr(async_ctrl->cmdlist_dev_id,
			async_ctrl->cmdlist_scene_id, payload_id);
		if (phy_addr == 0) {
			dpu_pr_err("cmdlist phy addr is null");
			return MIPI_E_UNKNOWN;
		}

		async_ctrl->cmdlists[i].apply_status = NO_APPLY;
		async_ctrl->cmdlists[i].cmdlist_header = header_id;
		async_ctrl->cmdlists[i].cmdlist_payload = payload_id;
		async_ctrl->cmdlists[i].cmdlist_phy_addr = phy_addr;
		async_ctrl->cmdlists[i].cmds_type = DKMD_CMDS_TYPE_MAX;

		dpu_pr_debug("request cmdlist_header = %d, payload_id = %d, phy_addr = 0x%x",
			async_ctrl->cmdlists[i].cmdlist_header,
			async_ctrl->cmdlists[i].cmdlist_payload,
			async_ctrl->cmdlists[i].cmdlist_phy_addr);
	}

	return 0;
}

void mipi_dsi_async_deinit(struct mipi_dsi_async_ctrl *async_ctrl)
{
	if (async_ctrl->async_status == ASYNC_UNINITIALIZED) {
		dpu_pr_debug("async is not initialized");
		return;
	}

	cmdlists_destroy(async_ctrl);
	async_ctrl->async_status = ASYNC_UNINITIALIZED;
}

static void mipi_dsi_async_dump_unsend_cmdlist(struct mipi_dsi_async_ctrl *async_ctrl)
{
	uint32_t i = 0;
	for (i = 0; i < MIPI_ASYNC_TX_MAX_NUM; i++) {
		if (async_ctrl->cmdlists[i].apply_status != NO_APPLY) {
			dpu_pr_warn("unsend cmdlist_header = %d, payload_id = %d",
				async_ctrl->cmdlists[i].cmdlist_header,
				async_ctrl->cmdlists[i].cmdlist_payload);
#ifdef CONFIG_UKMD_DEBUG_ENABLE
			ukmd_cmdlist_dump_by_id(async_ctrl->cmdlist_dev_id, async_ctrl->cmdlist_scene_id,
				async_ctrl->cmdlists[i].cmdlist_payload);
#endif
		}
	}
}

int32_t mipi_dsi_async_tx_stop(struct dpu_connector *connector)
{
	int32_t ret = 0;
	struct mipi_dsi_async_ctrl *async_ctrl = NULL;

	dpu_pr_debug("+");
	if (!connector) {
		dpu_pr_err("connector is NULL!");
		return MIPI_E_NULL_PTR;
	}
	async_ctrl = &connector->dsi_async_ctrl;
	if (async_ctrl->async_status != ASYNC_STARTED) {
		dpu_pr_debug("async tx already stoped");
		return 0;
	}

	async_ctrl->async_status = ASYNC_STOPED;
	outp32(DPU_ASYNC_TX_OPS_TYPE_ADDR(connector->dpu_base), TX_STOP);
	outp32(DPU_ASYNC_TX_OPS_STATUS_ADDR(connector->dpu_base), TX_OPS_REQ);
	/* trigger riscv interrupt, bit 5 used by async tx, unmasked by ltpo */
	outp32(DPU_RISCV_INTR_TRIG_ADDR(connector->dpu_base), 0x20);
	
	ret = wait_for_dmcu_ops_done(connector);
	mipi_dsi_async_dump_unsend_cmdlist(async_ctrl);
	return ret;
}

static uint32_t mipi_dsi_set_bit_val(uint32_t org_val, uint32_t set_val, uint8_t bw, uint8_t bs)
{
	uint32_t mask = (1UL << bw) - 1UL;
	uint32_t temp = org_val;
	temp &= ~(mask << bs);
	return  (temp | ((set_val & mask) << bs));
}

static void mipi_dsi_add_lp_to_cmdlist(uint32_t dev_id, uint32_t scene_id,uint32_t cmdlist_id,
	char __iomem *connector_base)
{
	uint32_t dsi_tx_mode_ctrl_lp = inp32(DPU_DSI_CMD_MODE_CTRL_ADDR(connector_base));
	dpu_pr_info("dsi_tx_mode_ctrl_value = %#x", dsi_tx_mode_ctrl_lp);
	dsi_tx_mode_ctrl_lp = mipi_dsi_set_bit_val(dsi_tx_mode_ctrl_lp, 0x7, 3, 8);
	dsi_tx_mode_ctrl_lp = mipi_dsi_set_bit_val(dsi_tx_mode_ctrl_lp, 0x1, 1, 14);
	dsi_tx_mode_ctrl_lp = mipi_dsi_set_bit_val(dsi_tx_mode_ctrl_lp, 0x3, 2, 16);
	dpu_pr_info("dsi_tx_mode_ctrl_lp = %#x", dsi_tx_mode_ctrl_lp);
	ukmd_set_reg(dev_id, scene_id, cmdlist_id, DPU_DSI_CMD_MODE_CTRL_ADDR(connector_base),
		dsi_tx_mode_ctrl_lp);
}

static void mipi_dsi_add_hs_to_cmdlist(uint32_t dev_id, uint32_t scene_id,uint32_t cmdlist_id,
	char __iomem *connector_base)
{
	uint32_t dsi_tx_mode_ctrl_hs = inp32(DPU_DSI_CMD_MODE_CTRL_ADDR(connector_base));
	dpu_pr_info("dsi_tx_mode_ctrl_value = %#x", dsi_tx_mode_ctrl_hs);
	dsi_tx_mode_ctrl_hs = mipi_dsi_set_bit_val(dsi_tx_mode_ctrl_hs, 0x0, 3, 8);
	dsi_tx_mode_ctrl_hs = mipi_dsi_set_bit_val(dsi_tx_mode_ctrl_hs, 0x0, 1, 14);
	dsi_tx_mode_ctrl_hs = mipi_dsi_set_bit_val(dsi_tx_mode_ctrl_hs, 0x0, 2, 16);
	dpu_pr_info("dsi_tx_mode_ctrl_hs = %#x", dsi_tx_mode_ctrl_hs);
	ukmd_set_reg(dev_id, scene_id, cmdlist_id, DPU_DSI_CMD_MODE_CTRL_ADDR(connector_base),
		dsi_tx_mode_ctrl_hs);
}

static uint32_t mipi_dsi_async_tx_cmdlist_delay(struct mipi_dsi_tx_params *params, uint32_t cur_index)
{
	uint32_t delay_us = 0;
	uint32_t cmdlist_last_index = 0;
	uint32_t wait = 0;
	uint32_t waittype = 0;

	if (params->power_mode == MIPI_DSI_POWER_MODE_LP || !(g_debug_dsi_lp_power_mode == 0))
		return ASYNC_LP_DELAY_US;

	cmdlist_last_index = params->write_cmds.cmds_num < cur_index + DSI_HP_FIFO_CMDS_MAX ?
		params->write_cmds.cmds_num - 1 : cur_index + DSI_HP_FIFO_CMDS_MAX - 1;
	wait = params->write_cmds.cmds[cmdlist_last_index].wait;

	waittype = params->write_cmds.cmds[cmdlist_last_index].waittype;
	if (waittype == 0) {
		if (wait < ASYNC_HS_DELAY_MAX_US) {
			delay_us = wait;
		} else {
			dpu_pr_warn("wait %d us is too long, force to %d us", wait, ASYNC_HS_DELAY_MAX_US);
			delay_us = ASYNC_HS_DELAY_MAX_US;
		}
	} else {
		dpu_pr_warn("waittype ms is not support, use default delay");
		/* if delay_us == 0, dacc use default delay 31.25us */
		delay_us = 0;
	}

	dpu_pr_debug("cmdlist_last_index = %u, wait = %u, waittype = %u, delay_us = %u",
		cmdlist_last_index, wait, waittype, delay_us);
	return delay_us;
}

static uint32_t async_tx_real_time(uint32_t delay_us)
{
    uint32_t timer_cnt = 0;
	uint32_t delay_ns = delay_us * 1000;
    timer_cnt = delay_ns / TIME_PER_CYCLE_NS;
    if (delay_ns % TIME_PER_CYCLE_NS > 0 )
        timer_cnt++;

    if (timer_cnt < TIMER_STEP_COUNT_MIN)
        timer_cnt = TIMER_STEP_COUNT_MIN;

    if(timer_cnt > TIMER_STEP_COUNT_MAX)
        timer_cnt = TIMER_STEP_COUNT_MAX;

    return timer_cnt * TIME_PER_CYCLE_NS / 1000;
}

static uint32_t mipi_dsi_calc_hardware_wait_cnt(struct dpu_connector *connector, struct mipi_dsi_tx_params *params, uint32_t cur_index, uint32_t dealy_us)
{
	struct mipi_panel_info *mipi = &connector->post_info[connector->active_idx]->mipi;
	uint32_t count = 0;
	uint64_t lane_byte_clk = (mipi->phy_mode == DPHY_MODE) ? ((uint64_t)2 * mipi->dsi_bit_clk_upt / 8) :
		((uint64_t)mipi->dsi_bit_clk_upt / 7);
	uint32_t cmd_nums = ((cur_index + DSI_HP_FIFO_CMDS_MAX) <= params->write_cmds.cmds_num) ? DSI_HP_FIFO_CMDS_MAX :
		(params->write_cmds.cmds_num % DSI_HP_FIFO_CMDS_MAX);
	uint32_t hardware_time = params->hardware_wait;
	/* ASYNC_CMD_SEND_TIME_US: consider the time that sending async cmds from fifo to dsi */
	uint32_t total_hardware_time = (hardware_time + ASYNC_CMD_SEND_TIME_US) * cmd_nums;
	uint32_t real_dealy_us = async_tx_real_time(dealy_us);
	if (total_hardware_time > real_dealy_us) {
		dpu_pr_warn("mipi hardware time need change, time = %u, cmd_nums = %u", hardware_time, cmd_nums);	
		hardware_time = ((real_dealy_us / cmd_nums) >= ASYNC_CMD_SEND_TIME_US) ?
			((real_dealy_us / cmd_nums) - ASYNC_CMD_SEND_TIME_US) : 0;
		dpu_pr_warn("mipi final hardware time = %u, real_dealy_us = %u", hardware_time, real_dealy_us);
	}

	dpu_pr_debug("lane_byte_clk = %llu M , phy_ctrl->lane_byte_clk = %llu M, hardware_wait = %u, cmd_nums = %u",
			lane_byte_clk, mipi->dsi_bit_clk_upt, hardware_time, cmd_nums);
	count = (uint32_t)((uint64_t)hardware_time * lane_byte_clk);

	return count;
}

static uint32_t mipi_dsi_async_tx_apply_cmdlist(struct dpu_connector *connector,
	struct mipi_dsi_tx_params *params, uint32_t cmd_index)
{
	uint32_t delay_us = 0;
	uint32_t cmds_flag = 0;
	uint32_t hardware_wait_cnt = 0;
	uint32_t cmdlist_group_pos = 0;
	struct mipi_dsi_async_ctrl *async_ctrl = &connector->dsi_async_ctrl;

	if (unlikely((async_ctrl->idle_cmdlist_group == 0) || (async_ctrl->idle_cmdlist_group > MIPI_ASYNC_TX_MAX_NUM_OF_GROUP))) {
		dpu_pr_err("Idle cmdlist group 0x%X is invalid", async_ctrl->idle_cmdlist_group);
		return MIPI_ASYNC_TX_MAX_NUM;
	}

	cmdlist_group_pos = (uint32_t)__builtin_ffs(async_ctrl->idle_cmdlist_group) - 1;
	if (unlikely(!cmdlist_apply(async_ctrl, cmdlist_group_pos))) {
		dpu_pr_err("Apply idle cmdlist failed");
		return MIPI_ASYNC_TX_MAX_NUM;
	}
	async_ctrl->idle_cmdlist_group &= ~BIT(cmdlist_group_pos);
	dpu_pr_info("request cmdlist_header = %u, cmds_type = %d",
		async_ctrl->cmdlists[cmdlist_group_pos].cmdlist_header, params->cmds_type);

	delay_us = mipi_dsi_async_tx_cmdlist_delay(params, cmd_index);
	hardware_wait_cnt = mipi_dsi_calc_hardware_wait_cnt(connector, params, cmd_index, delay_us);
	async_ctrl->cmdlists[cmdlist_group_pos].cmds_type = params->cmds_type;
	if (params->has_refresh) {
		cmds_flag |= CMDS_HAS_REFRESH;
	}

	if (params->follow_frame) {
		cmds_flag |= CMDS_FOLLOW_FRAME;
	}

	outp32(DPU_ASYNC_TX_REQ_CMDLIST_PHY_ADDR(connector->dpu_base, cmdlist_group_pos),
		async_ctrl->cmdlists[cmdlist_group_pos].cmdlist_phy_addr);
	outp32(DPU_ASYNC_TX_REQ_CMDLIST_TYPE_ADDR(connector->dpu_base, cmdlist_group_pos),
		params->cmds_type);
	outp32(DPU_ASYNC_TX_REQ_CMDLIST_DELAY_ADDR(connector->dpu_base, cmdlist_group_pos),
		delay_us);
	outp32(DPU_ASYNC_TX_REQ_CMDLIST_FLAG_ADDR(connector->dpu_base, cmdlist_group_pos),
		cmds_flag);
	outp32(DPU_ASYNC_TX_REQ_CMDS_VALIDATE_TYPE_ADDR(connector->dpu_base, cmdlist_group_pos),
		params->validate_repetition);

	mipi_dsi_set_dacc_hardware_cnt(connector, cmdlist_group_pos, hardware_wait_cnt);

	dpu_pr_debug("is flw frame %d, has refresh %d, validate repetition %d", params->follow_frame, params->has_refresh,
		params->validate_repetition);
	return cmdlist_group_pos;
}

static void mipi_dsi_async_recycle_cmdlist_group(struct dpu_connector *connector, uint32_t rel_cmdlist_group)
{
	uint32_t cmdlist_group_pos = 0;
	struct mipi_dsi_async_ctrl *async_ctrl = NULL;
	if (unlikely(rel_cmdlist_group > MIPI_ASYNC_TX_MAX_NUM_OF_GROUP)) {
		dpu_pr_warn("release cmdlist group %u is invalid", rel_cmdlist_group);
		return;
	}

	async_ctrl = &connector->dsi_async_ctrl;
	while (rel_cmdlist_group != 0) {
		cmdlist_group_pos = (uint32_t)__builtin_ffs(rel_cmdlist_group) - 1;
		rel_cmdlist_group &= ~(BIT(cmdlist_group_pos));
		cmdlist_recycle(async_ctrl, cmdlist_group_pos);
		dpu_pr_info("give back cmdlist_header_id = %u", async_ctrl->cmdlists[cmdlist_group_pos].cmdlist_header);
	}
}

static void mipi_dsi_async_recycle_idle_cmdlist(struct dpu_connector *connector, uint32_t rel_cmdlist_group)
{
	uint32_t idle_cmdlist_group = read_reg_with_retry(DPU_ASYNC_TX_IDLE_CMDLIST_GROUP_ADDR(connector->dpu_base));
	if (unlikely(idle_cmdlist_group == DSS_ERR_CODE_DEADBEED)) {
		dpu_pr_err("idle cmdlist group is invalid");
		return;
	}
	dpu_pr_debug("idle cmdlist group is %x, release cmdlist group is %x", idle_cmdlist_group, rel_cmdlist_group);
	mipi_dsi_async_recycle_cmdlist_group(connector, idle_cmdlist_group & rel_cmdlist_group);
}

static void mipi_dsi_async_tx_add_cmd_to_cmdlist(struct dpu_connector *connector, struct mipi_dsi_tx_params *params,
	uint32_t cmdlist_group_pos, uint32_t cmd_index)
{
	struct mipi_dsi_async_ctrl *async_ctrl = &connector->dsi_async_ctrl;
	struct mipi_dsi_cmdlist_desc *cmdlist_desc = &async_ctrl->cmdlists[cmdlist_group_pos];

	if ((cmd_index == 0) && g_debug_dsi_lp_power_mode) {
		dpu_pr_info("MIPI_DSI_POWER_MODE_LP index 0");
		mipi_dsi_add_lp_to_cmdlist(async_ctrl->cmdlist_dev_id, async_ctrl->cmdlist_scene_id,
			cmdlist_desc->cmdlist_payload, connector->connector_base);
	}

	mipi_dsi_cmd_add_to_cmdlist(async_ctrl->cmdlist_dev_id, async_ctrl->cmdlist_scene_id,
		cmdlist_desc->cmdlist_payload, g_connector_offset[connector->connector_id], &params->write_cmds.cmds[cmd_index]);
}

static void mipi_dsi_async_tx_print_debug_info(struct dpu_connector *connector)
{
	dpu_pr_debug("init_ok=%d, start_cnt=%d, received_cnt=%d, timer_cnt=%d, task_done=%d, sent_task=%d",
		inp32(DPU_ASYNC_TX_INIT_OK_ADDR(connector->dpu_base)),
		inp32(DPU_ASYNC_TX_START_CNT_ADDR(connector->dpu_base)),
		inp32(DPU_ASYNC_TX_RECEIVED_TASK_CNT_ADDR(connector->dpu_base)),
		inp32(DPU_ASYNC_TX_TIMER_CNT_ADDR(connector->dpu_base)),
		inp32(DPU_ASYNC_TX_TASK_DONE_CNT_ADDR(connector->dpu_base)),
		inp32(DPU_ASYNC_TX_SENT_TASK_CNT_ADDR(connector->dpu_base)));
}

static int32_t mipi_dsi_async_tx_send_tasks_set_reg(struct dpu_connector *connector, uint32_t apply_cmdlist_group)
{
	struct composer *comp = container_of(connector->conn_info->base.comp_obj_info, struct composer, base);

	if (!comp) {
		dpu_pr_err("comp is nullptr!");
		return MIPI_E_NULL_PTR;
	}

	outp32(DPU_ASYNC_TX_REQ_SCENE_ADDR(connector->dpu_base), comp->index);
	outp32(DPU_ASYNC_TX_REQ_CMDLIST_GROUP_ADDR(connector->dpu_base), apply_cmdlist_group);
	outp32(DPU_ASYNC_TX_OPS_TYPE_ADDR(connector->dpu_base), TX_SEND_TASKS);
	outp32(DPU_ASYNC_TX_OPS_STATUS_ADDR(connector->dpu_base), TX_OPS_REQ);

	/* trigger riscv interrupt, bit 5 used by async tx, unmasked by ltpo */
	outp32(DPU_RISCV_INTR_TRIG_ADDR(connector->dpu_base), 0x20);
	return 0;
}

static void mipi_dsi_async_tx_set_validate_repetition(uint32_t cmdlist_num, struct mipi_dsi_tx_params *params)
{
	if (cmdlist_num == 1 && (params->cmds_type == DKMD_CMDS_TYPE_BL || params->cmds_type == DKMD_CMDS_TYPE_SFR)) {
		params->validate_repetition = CMDS_ORDER_FIRST;
	} else {
		params->validate_repetition = CMDS_NO_VALIDATE;
	}
}

static void mipi_dsi_async_tx_async_recovery_handle(struct dpu_connector *connector)
{
	struct composer* comp = NULL;
	struct dpu_composer *dpu_comp = NULL;

	if (unlikely(connector->conn_info == NULL || connector->conn_info->base.comp_obj_info == NULL)) {
		dpu_pr_err("conn_info or comp_obj_info is nullptr!");
		return;
	}

	comp = container_of(connector->conn_info->base.comp_obj_info, struct composer, base);
	if (unlikely(comp == NULL)) {
		dpu_pr_err("comp is nullptr!");
		return;
	}

	if ((comp->comp_err_status & ABNORMAL_ASYNC_MIPI_WAIT_DMCU_TIMEOUT) != 0)
		return;

	dpu_comp = to_dpu_composer(comp);
	if (unlikely(dpu_comp == NULL)) {
		dpu_pr_err("dpu_comp is nullptr!");
		return;
	}
	comp->comp_err_status |= ABNORMAL_ASYNC_MIPI_WAIT_DMCU_TIMEOUT;
	kthread_queue_work(&dpu_comp->handle_worker, &connector->dsi_async_ctrl.recovery_handle_work);
}

static void mipi_dsi_async_tx_post_handle(struct dpu_connector *connector, uint32_t apply_cmdlist_group)
{
	uint32_t cmdlist_group_pos = 0;
	unsigned long flags = 0;
	struct mipi_dsi_async_ctrl *async_ctrl = &connector->dsi_async_ctrl;
	/* If cmdlist status is appling and the idle bit is idle, set cmdlist to not apply status. */
	mipi_dsi_async_recycle_idle_cmdlist(connector, apply_cmdlist_group);

	/* If cmdlist status is appling and the idle bit is busy, set cmdlist to applied status. */
	spin_lock_irqsave(&async_ctrl->cmdlist_lock, flags);
	while (apply_cmdlist_group != 0) {
		cmdlist_group_pos = (uint32_t)__builtin_ffs(apply_cmdlist_group) - 1;
		apply_cmdlist_group &= ~(BIT(cmdlist_group_pos));
		if (async_ctrl->cmdlists[cmdlist_group_pos].apply_status == APPLING) {
			async_ctrl->cmdlists[cmdlist_group_pos].applied_timestamp = ktime_get();
			async_ctrl->cmdlists[cmdlist_group_pos].apply_status = APPLIED;
			async_ctrl->applied_cmdlist_group |= BIT(cmdlist_group_pos);
		}
	}
	spin_unlock_irqrestore(&async_ctrl->cmdlist_lock, flags);
}

static int32_t mipi_dsi_async_tx_pre_handle(struct dpu_connector *connector, struct mipi_dsi_tx_params *params)
{
	uint32_t req_cmdlist_num = 0;
	struct mipi_dsi_async_ctrl *async_ctrl = &connector->dsi_async_ctrl;

	if (async_ctrl->async_status != ASYNC_STARTED) {
		dpu_pr_info("async tx stoped");
		return MIPI_E_ASYNC_STOPED;
	}

	if (g_debug_enable_async_recovery != 0) {
		mipi_dsi_async_tx_async_recovery_handle(connector);
		return MIPI_E_TIMEOUT;
	}

	req_cmdlist_num = calc_cmdlist_num(params->write_cmds.cmds_num);
	mipi_dsi_async_tx_set_validate_repetition(req_cmdlist_num, params);
	if (mipi_dsi_async_tx_is_available_cmdlist(async_ctrl, req_cmdlist_num)) {
		mipi_dsi_async_tx_update_idle_cmdlist(async_ctrl);
		return 0;
	}

	dpu_pr_info("Cmdlist fifo is full, try to recycle idle cmdlist");
	/* Make sure cmdlist can be recycled even if mdp interrupt delay */
	mipi_dsi_async_recycle_idle_cmdlist(connector, async_ctrl->applied_cmdlist_group);
	if (mipi_dsi_async_tx_is_available_cmdlist(async_ctrl, req_cmdlist_num)) {
		mipi_dsi_async_tx_update_idle_cmdlist(async_ctrl);
		return 0;
	}

	dpu_pr_warn("Cmdlist fifo is full, cur applied cmdlist nums=%u, need cmdlist nums=%u",
		async_ctrl->applied_cmdlist_group, req_cmdlist_num);

	/* Recovery dss when half async cmdlist timeout */
	if (is_cmdlists_apply_timeout(async_ctrl)) {
		dpu_pr_err("Async module in abnormal state, need recover!");
		mipi_dsi_async_tx_async_recovery_handle(connector);
		return MIPI_E_TIMEOUT;
	}

	return MIPI_E_TOO_MANY_TXS;
}

static int32_t mipi_dsi_async_tx_send_tasks(struct dpu_connector *connector, uint32_t apply_cmdlist_group)
{
	int32_t ret = 0;
	mipi_dsi_async_tx_print_debug_info(connector);
	ret = mipi_dsi_async_tx_send_tasks_set_reg(connector, apply_cmdlist_group);
	if (ret == 0)
		ret = wait_for_dmcu_ops_done(connector);

	mipi_dsi_async_tx_post_handle(connector, apply_cmdlist_group);
	return ret;
}

int32_t mipi_dsi_async_tx(struct dpu_connector *connector, struct mipi_dsi_tx_params *params)
{
	int32_t ret = 0;
	uint32_t cmd_idx = 0;
	uint32_t cmdlist_group_pos = 0;
	uint32_t apply_cmdlist_group = 0;

	ret = mipi_dsi_async_tx_pre_handle(connector, params);
	if (ret != 0)
		return ret;

	for (cmd_idx = 0; cmd_idx < params->write_cmds.cmds_num; cmd_idx++) {
		if ((cmd_idx % DSI_HP_FIFO_CMDS_MAX) == 0) {
			cmdlist_group_pos = mipi_dsi_async_tx_apply_cmdlist(connector, params, cmd_idx);
			if (cmdlist_group_pos == MIPI_ASYNC_TX_MAX_NUM) {
				dpu_pr_err("Not enough cmdlist");
				mipi_dsi_async_recycle_cmdlist_group(connector, apply_cmdlist_group);
				return MIPI_E_NULL_PTR;
			}
			apply_cmdlist_group |= (uint32_t)(BIT(cmdlist_group_pos));
		}
		mipi_dsi_async_tx_add_cmd_to_cmdlist(connector, params, cmdlist_group_pos, cmd_idx);
	}

	return mipi_dsi_async_tx_send_tasks(connector, apply_cmdlist_group);
}

void mipi_dsi_async_tx_done(struct dpu_connector *connector)
{
	uint32_t tx_status = 0;
	uint32_t flw_task_left_num = 0;

	mipi_dsi_async_tx_print_debug_info(connector);
	tx_status = read_reg_with_retry(DPU_ASYNC_TX_ERROR_STATUS(connector->dpu_base));
	if (tx_status != 0)
		dpu_pr_info("tx_status: %u", tx_status);

	flw_task_left_num = read_reg_with_retry(DPU_ASYNC_TX_FLW_TASK_LEFT_NUM_ADDR(connector->dpu_base));
	if (flw_task_left_num > 0)
		dpu_pr_warn("flw task left num: %d", flw_task_left_num);

	mipi_dsi_async_recycle_idle_cmdlist(connector, connector->dsi_async_ctrl.applied_cmdlist_group);
}

static void dual_mipi_dsi_async_tx_add_cmd_to_cmdlist(struct dpu_connector *connector0,
	struct dpu_connector *connector1, struct mipi_dsi_tx_params *dsi0_params, struct mipi_dsi_tx_params *dsi1_params,
	uint32_t cmdlist_group_pos, uint32_t cmd_index)
{
	struct mipi_dsi_async_ctrl *async_ctrl = &connector0->dsi_async_ctrl;
	struct mipi_dsi_cmdlist_desc *cmdlist_desc = &async_ctrl->cmdlists[cmdlist_group_pos];

	if ((cmd_index == 0) && g_debug_dsi_lp_power_mode) {
		dpu_pr_info("MIPI_DSI_POWER_MODE_LP index 0");
		mipi_dsi_add_lp_to_cmdlist(async_ctrl->cmdlist_dev_id, async_ctrl->cmdlist_scene_id,
			cmdlist_desc->cmdlist_payload, connector0->connector_base);
		mipi_dsi_add_lp_to_cmdlist(async_ctrl->cmdlist_dev_id, async_ctrl->cmdlist_scene_id,
			cmdlist_desc->cmdlist_payload, connector1->connector_base);
	}

	mipi_dsi_cmd_add_to_cmdlist(async_ctrl->cmdlist_dev_id, async_ctrl->cmdlist_scene_id,
		cmdlist_desc->cmdlist_payload, g_connector_offset[connector0->connector_id],
		&dsi0_params->write_cmds.cmds[cmd_index]);
	mipi_dsi_cmd_add_to_cmdlist(async_ctrl->cmdlist_dev_id, async_ctrl->cmdlist_scene_id,
		cmdlist_desc->cmdlist_payload, g_connector_offset[connector1->connector_id],
		&dsi1_params->write_cmds.cmds[cmd_index]);
}

int32_t dual_mipi_dsi_async_tx(struct dpu_connector *connector0, struct dpu_connector *connector1,
	struct mipi_dsi_tx_params *dsi0_params, struct mipi_dsi_tx_params *dsi1_params)
{
	int32_t ret = 0;
	uint32_t cmd_idx = 0;
	uint32_t cmdlist_group_pos = 0;
	uint32_t apply_cmdlist_group = 0;

	ret = mipi_dsi_async_tx_pre_handle(connector0, dsi0_params);
	if (ret != 0)
		return ret;

	for (cmd_idx = 0; cmd_idx < dsi0_params->write_cmds.cmds_num; cmd_idx++) {
		if ((cmd_idx % DSI_HP_FIFO_CMDS_MAX) == 0) {
			cmdlist_group_pos = mipi_dsi_async_tx_apply_cmdlist(connector0, dsi0_params, cmd_idx);
			if (cmdlist_group_pos == MIPI_ASYNC_TX_MAX_NUM) {
				dpu_pr_err("Not enough cmdlist");
				mipi_dsi_async_recycle_cmdlist_group(connector0, apply_cmdlist_group);
				return MIPI_E_NULL_PTR;
			}
			apply_cmdlist_group |= BIT(cmdlist_group_pos);
		}
		dual_mipi_dsi_async_tx_add_cmd_to_cmdlist(connector0, connector1, dsi0_params, dsi1_params, cmdlist_group_pos, cmd_idx);
	}

	return mipi_dsi_async_tx_send_tasks(connector0, apply_cmdlist_group);
}
