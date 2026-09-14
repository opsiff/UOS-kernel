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

#include <linux/smp.h>
#include "mipi_dsi_sync.h"
#include "mipi_dsi_dev.h"
#include "dkmd_object.h"
#include "dkmd_log.h"
#include "dpu_conn_mgr.h"
#include "mipi_config_utils.h"
#include <platform_include/basicplatform/linux/dfx_bbox_diaginfo.h>

struct dsi_send_cmds_info {
	struct dpu_connector *connector;
	struct dsi_cmd_desc *cmds;
	uint32_t start_index;
	uint32_t end_index;
	ktime_t win_end;
};

struct dsi_read_info {
	struct dpu_connector *connector;
	struct dsi_cmd_desc *cmds;
	uint32_t cmd_index;
	struct mipi_dsi_out *read_outs;
	bool is_little_endian;
};

int32_t mipi_dsi_sync_write(struct dpu_connector *connector, struct mipi_dsi_tx_params *params)
{
	uint32_t i;
	struct mipi_dsi_cmds *dsi_cmds = &params->write_cmds;

	if (!atomic_read(&connector->mipi_dsi_on_flag)) {
		dpu_pr_warn("mipi dsi is power off");
		return MIPI_E_POWER_OFF;
	}

	if (mipi_dsi_fifo_is_full(connector->connector_base)) {
		dpu_pr_err("wait 10ms for fifo consuming, but failed");
		return MIPI_E_FIFO_FULL;
	}

	/* if dsi power mode is enhanced, we don't need chang power mode */
	if (params->power_mode == MIPI_DSI_POWER_MODE_SINGLE) {
		mipi_dsi_set_interval(connector, params->hardware_wait, dsi_cmds->cmds_num);
		for (i = 0; i < dsi_cmds->cmds_num; i++) {
			mipi_dsi_cmd_add(&dsi_cmds->cmds[i], connector->connector_base);
			delay_for_next_cmd(dsi_cmds->cmds[i].wait, dsi_cmds->cmds[i].waittype);
		}
		return 0;
	}

	/* default is high speed mode, change to low power mode */
	if (params->power_mode == MIPI_DSI_POWER_MODE_LP)
		mipi_dsi_tx_lp_mode_cfg(connector->connector_base);

	mipi_dsi_set_interval(connector, params->hardware_wait, dsi_cmds->cmds_num);
	for (i = 0; i < dsi_cmds->cmds_num; i++) {
		mipi_dsi_cmd_add(&dsi_cmds->cmds[i], connector->connector_base);
		delay_for_next_cmd(dsi_cmds->cmds[i].wait, dsi_cmds->cmds[i].waittype);
	}

	/* resume to high speed mode */
	if (params->power_mode == MIPI_DSI_POWER_MODE_LP)
		mipi_dsi_tx_hs_mode_cfg(connector->connector_base);

	return 0;
}

int32_t mipi_dsi_sync_read(struct dpu_connector *connector, struct mipi_dsi_rx_params *params)
{
	int ret;
	uint32_t tmp_value[READ_MAX] = {0};

	struct dsi_cmd_desc *cmd = &params->read_cmd;
	struct mipi_dsi_out *dsi_out = &params->read_outs;

	if (!atomic_read(&connector->mipi_dsi_on_flag) || !atomic_read(&connector->mipi_dsi_read_enable)) {
		dpu_pr_warn("mipi_dsi_on_flag %d, mipi_dsi_read_enable %d", atomic_read(&connector->mipi_dsi_on_flag),
			atomic_read(&connector->mipi_dsi_read_enable));
		return MIPI_E_POWER_OFF;
	}

	if (mipi_dsi_fifo_is_full(connector->connector_base)) {
		dpu_pr_err("wait 10ms for fifo consuming, but failed");
		return MIPI_E_FIFO_FULL;
	}
	dpu_pr_debug("power mode = %d", params->power_mode);

	if (params->power_mode == MIPI_DSI_POWER_MODE_LP)
		mipi_dsi_rx_lp_mode_cfg(connector->connector_base);

	ret = mipi_dsi_lread_reg(tmp_value, READ_MAX, cmd, cmd->dlen, connector->connector_base);

	if (params->power_mode == MIPI_DSI_POWER_MODE_LP)
		mipi_dsi_rx_hs_mode_cfg(connector->connector_base);

	if (ret) {
		dpu_pr_err("mipi read error");
		return ret;
	}

	ret = mipi_dsi_get_read_value(cmd, dsi_out->out, tmp_value, (uint32_t)dsi_out->out_len, params->is_little_endian);
	if (ret < 0) {
		dpu_pr_err("get read value error");
		return ret;
	}

	connector->need_check_mipi_connected = false;
	return 0;
}

static bool need_to_wait_time_win(struct dpu_connector *connector)
{
	return atomic_read(&connector->need_wait_window) && (atomic64_read(&connector->cmds_window_start_timestamp) != -1);
}

static ktime_t get_cmds_win_start(struct dpu_connector *connector)
{
	return atomic64_read(&connector->cmds_window_start_timestamp);
}

static ktime_t get_cmds_win_end(struct dpu_connector *connector)
{
	return atomic64_read(&connector->cmds_window_end_timestamp);
}

static bool is_dsi_fifo_full(const char __iomem *dsi_base)
{
	uint32_t pkg_status = inp32(DPU_DSI_CMD_PLD_BUF_STATUS_ADDR(dsi_base));
	uint32_t phy_status = inp32(DPU_DSI_CDPHY_STATUS_ADDR(dsi_base));
	if ((pkg_status & 0x2) == 0x2 || ((phy_status & 0x2) == 0x2))
		return true;

	return false;
}

static void mipi_dsi_send_read_cmd(struct dsi_cmd_desc *cm, char __iomem *dsi_base)
{
	struct dsi_cmd_desc packet_size_cmd_set;

	packet_size_cmd_set.dtype = DTYPE_MAX_PKTSIZE;
	packet_size_cmd_set.vc = 0;
	packet_size_cmd_set.dlen = cm->dlen;

	mipi_dsi_max_return_packet_size(&packet_size_cmd_set, dsi_base);
	mipi_dsi_sread_request(cm, dsi_base);
}

static uint32_t split_cmds_to_sub_group(struct mipi_dsi_cmds *read_cmds, uint32_t index[][2])
{
	uint32_t j = 0;
	uint32_t i = 0;

	index[0][0] = 0;

	for (i = 0; i < read_cmds->cmds_num; i++) {
		if (mipi_dsi_cmd_is_read(&read_cmds->cmds[i])) {
			index[j][1] = i;
			if (i != read_cmds->cmds_num - 1) {
				j++;
				index[j][0] = i + 1;
			}
		}
	}

	index[j][1] = read_cmds->cmds_num - 1;
	return j + 1;
}

static uint32_t get_out_buffer_offset(struct dsi_cmd_desc *cmds, uint32_t cmd_index)
{
	uint32_t i = 0;
	uint32_t offset = 0;

	for (i = 0; i < cmd_index; i++) {
		if (mipi_dsi_cmd_is_read(&cmds[i]))
			offset += cmds[i].dlen;
	}
	return offset;
}

static bool read_back_value(struct dsi_read_info *read_info)
{
	uint32_t i = 0;
	struct dsi_cmd_desc *cmd = &read_info->cmds[read_info->cmd_index];
	uint32_t tmp_value[READ_MAX] = {0};
	uint32_t offset = get_out_buffer_offset(read_info->cmds, read_info->cmd_index);

	for (i = 0; i < ceil_div(cmd->dlen, 4); i++) {
		if (mipi_dsi_read(&tmp_value[i], read_info->connector->connector_base,
			MIPI_DSI_READ_CHECK_WAIT, MIPI_DDIC_READ_BACK_TIMEOUT) == 0) {
			dpu_pr_err("Read ddic register timeout, cmd %u", read_info->cmd_index);
			return false;
		}
	}

	if (mipi_dsi_get_read_value(cmd, read_info->read_outs->out + offset, tmp_value,
		(uint32_t)read_info->read_outs->out_len - offset, read_info->is_little_endian) <= 0) {
		dpu_pr_err("get read value error, cmd %u", read_info->cmd_index);
		return false;
	}

	return true;
}

static bool sub_group_cmds_send_handle(char __iomem *dsi_base, struct dsi_cmd_desc *cmds, uint32_t start_index,
	uint32_t end_index)
{
	uint32_t i = 0;
	for (i = start_index; i <= end_index; i++) {
		if (is_dsi_fifo_full(dsi_base)) {
			dpu_pr_warn("dsi fifo full %u", start_index);
			return false;
		}

		if (mipi_dsi_cmd_is_write(&cmds[i]))
			mipi_dsi_cmd_add_nolock(&cmds[i], dsi_base);
		else
			mipi_dsi_send_read_cmd(&cmds[i], dsi_base);
	}

	return true;
}

static void sub_group_cmds_send(void *data)
{
	ktime_t t0;
	ktime_t t1;
	int64_t exec_time = 0;
	int64_t exceed_time = 0;
	struct dsi_send_cmds_info *info = (struct dsi_send_cmds_info *)data;

	t0 = ktime_get();
	if ((info->win_end != -1) && (t0 > info->win_end)) {
		dpu_pr_warn("sub_group timeout %u post end %lld", info->start_index, ktime_us_delta(t0, info->win_end));
		info->connector->cmds_send_succ = false;
		return;
	}

	if (!sub_group_cmds_send_handle(info->connector->connector_base, info->cmds, info->start_index, info->end_index)) {
		info->connector->cmds_send_succ = false;
		return;
	}

	if (info->connector->bind_connector && !sub_group_cmds_send_handle(info->connector->bind_connector->connector_base,
		info->cmds, info->start_index, info->end_index)) {
		info->connector->cmds_send_succ = false;
		return;
	}

	info->connector->cmds_send_succ = true;
	t1 = ktime_get();
	exec_time = ktime_us_delta(t1, t0);
	if ((info->win_end != -1) && (exec_time > MIPI_SYNC_ASYNC_CUSHION_TIME)) {
		exceed_time = ktime_us_delta(t1, ktime_add_us(info->win_end, MIPI_SYNC_ASYNC_CUSHION_TIME));
		dpu_pr_warn("cmd send cost %lld us, exceed sync %lld us", exec_time, exceed_time);

		if (exceed_time < 0)
			bbox_diaginfo_record(DMD_DPU_EXCEPTION, NULL, "[MIPI]sub group send timeout,send cost %lld us", exec_time);
		else
			bbox_diaginfo_record(DMD_DPU_EXCEPTION, NULL, "[MIPI]sub group send timeout,send cost %lld us, exceed sync %lld us",
				exec_time, exceed_time);
	}
}

#ifndef CONFIG_DKMD_DPU_OHOS
static bool sub_group_cmds_send_no_irq(struct dsi_send_cmds_info *info)
{
	local_irq_disable();
	sub_group_cmds_send(info);
	local_irq_enable();
	return info->connector->cmds_send_succ;
}
#else
static bool sub_group_cmds_send_no_irq(struct dsi_send_cmds_info *info)
{
	int cpu;
	cpumask_t nonself_cpus;

	/* For exec function without interrupted by other task,
	 * current cpu send IPI(inter-processor interrupt) to target cpu, target cpu exec function in IPI
	 * 1. use smp_call_function_single to send IPI.
	 * 2. target cpu can not be self, if target cpu is self, do not send IPI.
	 * 3. avoid cpu 0, because cpu 0 has many hardware interrputs
	 */
	cpumask_copy(&nonself_cpus, cpu_online_mask);
	cpumask_clear_cpu(get_cpu(), &nonself_cpus);
	cpumask_clear_cpu(0, &nonself_cpus);
	cpu = cpumask_first(&nonself_cpus);
	if (cpu >= nr_cpu_ids) {
		dpu_pr_err("cpu %d is invalid ", cpu);
		put_cpu();
        return false;
    }

	smp_call_function_single(cpu, sub_group_cmds_send, (void*)info, true);
	put_cpu();

	return info->connector->cmds_send_succ;
}
#endif

static bool is_last_write_cmd(struct dsi_send_cmds_info *send_cmds_info)
{
	return mipi_dsi_cmd_is_write(&send_cmds_info->cmds[send_cmds_info->end_index]);
}

static int32_t sub_group_cmds_exec(struct dsi_send_cmds_info *send_cmds_info, struct mipi_dsi_group_rx_params *params)
{
	struct dsi_read_info read_info = {0};
	if (!sub_group_cmds_send_no_irq(send_cmds_info))
		return MIPI_E_CMDS_SEND_FAILED;

	if (is_last_write_cmd(send_cmds_info))
		return 0;

	read_info.connector = send_cmds_info->connector;
	read_info.cmds = send_cmds_info->cmds;
	read_info.read_outs = &params->read_outs;
	read_info.cmd_index = send_cmds_info->end_index;
	read_info.is_little_endian = params->is_little_endian;
	if (!read_back_value(&read_info))
		return MIPI_E_READ_FAILED;

	return 0;
}

static bool wait_time_win(struct dpu_connector *connector, ktime_t *win_end, ktime_t timeout_timestamp)
{
	ktime_t call_time = ktime_get();

	do {
		if (!need_to_wait_time_win(connector)) {
			*win_end = -1;
			return true;
		}

		if (ktime_get() > timeout_timestamp) {
			dpu_pr_warn("wait_time_win timeout");
			return false;
		}

		udelay(MIPI_CHECK_WINDOW_TIME_PERIOD);
	} while (call_time > get_cmds_win_start(connector));

	*win_end =  get_cmds_win_end(connector);
	return true;
}

static int32_t sub_groups_exec(struct dpu_connector *connector, struct mipi_dsi_group_rx_params *params,
	uint32_t sub_group[][2], uint32_t sub_group_num)
{
	int32_t ret = 0;
	uint32_t i = 0;
	struct dsi_send_cmds_info send_cmds_info = {connector, params->read_cmds.cmds, 0, 0, -1};
	ktime_t timeout_timestamp = ktime_add_us(ktime_get(), MIPI_GROUP_RX_TIMEOUT);

	do {
		if (!wait_time_win(connector, &send_cmds_info.win_end, timeout_timestamp)) {
			dpu_pr_warn("group rx timeout, cmd %u", sub_group[i][0]);
			bbox_diaginfo_record(DMD_DPU_EXCEPTION, NULL, "[MIPI]group read timeout");
			ret = MIPI_E_READ_TOTAL_TIMEOUT;
			break;
		}

		for (; i < sub_group_num; i++) {
			dpu_pr_debug("sub_group exec %u - %u", sub_group[i][0], sub_group[i][1]);
			send_cmds_info.start_index = sub_group[i][0];
			send_cmds_info.end_index = sub_group[i][1];
			ret = sub_group_cmds_exec(&send_cmds_info, params);
			if (ret != 0)
				break;
		}

		if (ret == MIPI_E_READ_FAILED)
			break;
	} while (ret != 0);

	return ret;
}

int32_t mipi_dsi_group_sync_read(struct dpu_connector *connector, struct mipi_dsi_group_rx_params *params)
{
	uint32_t sub_group[DSI_GROUP_CMDS_NUM_MAX][2] = {0};
	uint32_t sub_group_num = 0;

	if (!atomic_read(&connector->mipi_dsi_on_flag) || !atomic_read(&connector->mipi_dsi_read_enable)) {
		dpu_pr_warn("mipi_dsi_on_flag %d, mipi_dsi_read_enable %d", atomic_read(&connector->mipi_dsi_on_flag),
			atomic_read(&connector->mipi_dsi_read_enable));
		return MIPI_E_POWER_OFF;
	}

	sub_group_num = split_cmds_to_sub_group(&params->read_cmds, sub_group);

	return sub_groups_exec(connector, params, sub_group, sub_group_num);
}

int32_t dual_mipi_dsi_sync_write(struct dpu_connector *connector0, struct dpu_connector *connector1,
	struct mipi_dsi_tx_params *dsi0_params, struct mipi_dsi_tx_params *dsi1_params)
{
	uint32_t i;
	dpu_pr_debug("+");
	if (!atomic_read(&connector0->mipi_dsi_on_flag)) {
		dpu_pr_warn("mipi dsi is power off");
		return MIPI_E_POWER_OFF;
	}

	if (mipi_dual_dsi_fifo_is_full(connector0->connector_base, connector1->connector_base)) {
		dpu_pr_err("wait 10ms for fifo consuming, but failed");
		return MIPI_E_FIFO_FULL;
	}

    /* if dsi power mode is enhanced, we don't need chang power mode */
	if (dsi0_params->power_mode == MIPI_DSI_POWER_MODE_SINGLE) {
		if (dsi0_params->write_cmds.cmds_num == dsi1_params->write_cmds.cmds_num) {
			mipi_dsi_set_interval(connector0, dsi0_params->hardware_wait, dsi0_params->write_cmds.cmds_num);
			for (i = 0; i < dsi0_params->write_cmds.cmds_num; i++) {
				mipi_dsi_cmd_add(&dsi0_params->write_cmds.cmds[i], connector0->connector_base);
				mipi_dsi_cmd_add(&dsi1_params->write_cmds.cmds[i], connector1->connector_base);
				delay_for_next_cmd(dsi0_params->write_cmds.cmds[i].wait, dsi0_params->write_cmds.cmds[i].waittype);
			}
		} else {
			dpu_pr_warn("diff cmd cnt, not support yet");
		}

		dpu_pr_debug("-");
        return 0;
	}

	if (dsi0_params->power_mode == MIPI_DSI_POWER_MODE_LP) {
		mipi_dsi_tx_lp_mode_cfg(connector0->connector_base);
		mipi_dsi_tx_lp_mode_cfg(connector1->connector_base);
	}

	if (dsi0_params->write_cmds.cmds_num == dsi1_params->write_cmds.cmds_num) {
		mipi_dsi_set_interval(connector0, dsi0_params->hardware_wait, dsi0_params->write_cmds.cmds_num);
		for (i = 0; i < dsi0_params->write_cmds.cmds_num; i++) {
			mipi_dsi_cmd_add(&dsi0_params->write_cmds.cmds[i], connector0->connector_base);
			mipi_dsi_cmd_add(&dsi1_params->write_cmds.cmds[i], connector1->connector_base);
			delay_for_next_cmd(dsi0_params->write_cmds.cmds[i].wait, dsi0_params->write_cmds.cmds[i].waittype);
		}
	} else {
		dpu_pr_warn("diff cmd cnt, not support yet");
	}

	if (dsi0_params->power_mode == MIPI_DSI_POWER_MODE_LP) {
		mipi_dsi_tx_hs_mode_cfg(connector0->connector_base);
		mipi_dsi_tx_hs_mode_cfg(connector1->connector_base);
	}
	dpu_pr_debug("-");
	return 0;
}

int32_t dual_mipi_dsi_sync_read(struct dpu_connector *connector0, struct dpu_connector *connector1,
    struct mipi_dsi_rx_params *dsi0_params, struct mipi_dsi_rx_params *dsi1_params)
{
	int32_t ret;
	struct mipi_dual_dsi_param dual_dsi0 = {0};
	struct mipi_dual_dsi_param dual_dsi1 = {0};
	uint32_t dsi0_tmp_val[READ_MAX] = {0};
	uint32_t dsi1_tmp_val[READ_MAX] = {0};

	dpu_pr_debug("+");
	if (!atomic_read(&connector0->mipi_dsi_on_flag)) {
		dpu_pr_warn("mipi dsi is power off");
		return MIPI_E_POWER_OFF;
	}

	dual_dsi0.dsi_base = connector0->connector_base;
	dual_dsi0.value_out = dsi0_tmp_val;
	dual_dsi1.dsi_base = connector1->connector_base;
	dual_dsi1.value_out = dsi1_tmp_val;

	ret = mipi_dual_dsi_lread_reg(&dual_dsi0, &dsi0_params->read_cmd, dsi0_params->read_cmd.dlen, &dual_dsi1);
	if (ret != 0) {
		dpu_pr_err("mipi read error");
		return -EINVAL;
	}

	ret = mipi_dsi_get_read_value(&dsi0_params->read_cmd, dsi0_params->read_outs.out, dsi0_tmp_val,
		dsi0_params->read_outs.out_len, dsi0_params->is_little_endian);
	if (ret < 0) {
		dpu_pr_err("get read value error");
		return ret;
	}

	ret = mipi_dsi_get_read_value(&dsi0_params->read_cmd, dsi1_params->read_outs.out, dsi1_tmp_val,
		dsi0_params->read_outs.out_len, dsi0_params->is_little_endian);
	if (ret < 0) {
		dpu_pr_err("get read value error");
		return ret;
	}

	dpu_pr_debug("-");
	return 0;
}

/*
 * dual mipi
 * 1st:send dsi0 and dsi1 cmd
 * 2nd:read back dsi0 and dsi1
 */
static int32_t dual_sub_group_cmds_exec(struct dsi_send_cmds_info *send_cmds_info,
	struct mipi_dsi_group_rx_params *params0, struct mipi_dsi_group_rx_params *params1)
{
	struct dsi_read_info read_info = {0};

	if (!sub_group_cmds_send_no_irq(send_cmds_info))
		return MIPI_E_CMDS_SEND_FAILED;

	if (is_last_write_cmd(send_cmds_info))
		return 0;

	read_info.connector = send_cmds_info->connector;
	read_info.cmds = send_cmds_info->cmds;
	read_info.read_outs = &params0->read_outs;
	read_info.cmd_index = send_cmds_info->end_index;
	read_info.is_little_endian = params0->is_little_endian;
	if (!read_back_value(&read_info)) {
		dpu_pr_err("read back value failed connector0");
		return MIPI_E_READ_FAILED;
	}

	read_info.connector = send_cmds_info->connector->bind_connector;
	read_info.read_outs = &params1->read_outs;
	if (!read_back_value(&read_info)) {
		dpu_pr_err("read back value failed connector1");
		return MIPI_E_READ_FAILED;
	}

	return 0;
}

int32_t dual_mipi_dsi_group_sync_read(struct dpu_connector *connector0, struct dpu_connector *connector1,
	struct mipi_dsi_group_rx_params *params0, struct mipi_dsi_group_rx_params *params1)
{
	uint32_t sub_group[DSI_GROUP_CMDS_NUM_MAX][2] = {0};
	uint32_t sub_group_num = 0;
	int32_t ret = 0;
	uint32_t i = 0;
	struct dsi_send_cmds_info send_cmds_info = {connector0, params0->read_cmds.cmds, 0, 0, -1};
	ktime_t timeout_timestamp = ktime_add_us(ktime_get(), MIPI_GROUP_RX_TIMEOUT);

	if (!atomic_read(&connector0->mipi_dsi_on_flag) || !atomic_read(&connector0->mipi_dsi_read_enable)) {
		dpu_pr_warn("mipi_dsi_on_flag %d, mipi_dsi_read_enable %d", atomic_read(&connector0->mipi_dsi_on_flag),
			atomic_read(&connector0->mipi_dsi_read_enable));
		return MIPI_E_POWER_OFF;
	}

	sub_group_num = split_cmds_to_sub_group(&params0->read_cmds, sub_group);

	do {
		if (!wait_time_win(connector0, &send_cmds_info.win_end, timeout_timestamp)) {
			dpu_pr_warn("group rx timeout, cmd %u", sub_group[i][0]);
			bbox_diaginfo_record(DMD_DPU_EXCEPTION, NULL, "[MIPI]group read timeout");
			ret = MIPI_E_READ_TOTAL_TIMEOUT;
			break;
		}

		for (; i < sub_group_num; i++) {
			dpu_pr_debug("sub_group exec %u - %u", sub_group[i][0], sub_group[i][1]);
			send_cmds_info.start_index = sub_group[i][0];
			send_cmds_info.end_index = sub_group[i][1];
			ret = dual_sub_group_cmds_exec(&send_cmds_info, params0, params1);
			if (ret != 0)
				break;
		}

		if (ret == MIPI_E_READ_FAILED)
			break;
	} while (ret != 0);

	return ret;
}