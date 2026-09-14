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

#include "dkmd_mipi_dsi_itf.h"
#include "mipi_dsi_sync.h"
#include "mipi_dsi_async.h"
#include "dpu_conn_mgr.h"
#include "dpu_comp_mgr.h"
#include "dksm_utils.h"

static int32_t mipi_dsi_tx_params_check(struct mipi_dsi_tx_params *params)
{
	if (unlikely(!params)) {
		dpu_pr_err("params is null");
		return -1;
	}

	if (unlikely(params->write_cmds.cmds_num > TX_CMD_MAX || params->write_cmds.cmds_num == 0)) {
		dpu_pr_err("cmds_num = %u is invalid", params->write_cmds.cmds_num);
		return -1;
	}

	if (unlikely(!params->write_cmds.cmds)) {
		dpu_pr_err("cmds is null");
		return -1;
	}

	return 0;
}

static int32_t mipi_dsi_rx_params_out_buffer_check(struct mipi_dsi_rx_params *params)
{
	if (unlikely(!params)) {
		dpu_pr_err("params is null");
		return -1;
	}

	if (unlikely(params->read_cmd.dlen > BUF_MAX || params->read_cmd.dlen == 0)) {
		dpu_pr_err("dlen = %u is invalid", params->read_cmd.dlen);
		return -1;
	}

	return 0;
}

static int32_t mipi_dsi_group_rx_params_check(struct mipi_dsi_group_rx_params *params)
{
	uint32_t i;
	uint32_t total_cmds_read_dlen = 0;
	struct mipi_dsi_cmds *cmds = NULL;
	struct dsi_cmd_desc *cmd = NULL;

	if (unlikely(!params)) {
		dpu_pr_err("params is null");
        return -1;
	}
	// check whether the is valid
	cmds = &params->read_cmds;
	if (unlikely(!cmds->cmds)){
		dpu_pr_err("cmd is null");
        return -1;
	}
	if (unlikely(cmds->cmds_num > DSI_GROUP_CMDS_NUM_MAX || cmds->cmds_num == 0)) {
		dpu_pr_err("cmds_num = %u is invalid", cmds->cmds_num);
        return -1;
	}
	// check whether read cmds is valid
	for (i = 0; i < cmds->cmds_num; i++) {
		if (mipi_dsi_cmd_is_read(&cmds->cmds[i])) {
			cmd = &cmds->cmds[i];
			if (unlikely(cmd->dlen > BUF_MAX || cmd->dlen == 0 ||
				ceil_div(cmd->dlen, 4) > READ_MAX)) {
				dpu_pr_err("dlen = %u is invalid", cmd->dlen);
				return -1;
			}

			if (unlikely(!cmd->payload)) {
				dpu_pr_err("payload is null");
                return -1;
			}
			total_cmds_read_dlen += cmd->dlen;
		} else if (!mipi_dsi_cmd_is_write(&cmds->cmds[i])) {
			dpu_pr_err("cmd is not read or write");
			return -1;
		}
	}
	// check whether read_outs is valid
	if (unlikely(!params->read_outs.out) || ((int32_t)total_cmds_read_dlen > params->read_outs.out_len)) {
		dpu_pr_err("out is null or out_len is invalid");
		return -1;
	}

	return 0;
}

int32_t dpu_mipi_dsi_tx(struct mipi_dsi_tx_params *params)
{
	int32_t ret = 0;
	struct dpu_connector *connector = NULL;

	if (mipi_dsi_tx_params_check(params) != 0) {
		return -1;
	}

	connector = get_real_connector(params->dsi_id, params->panel_type);
	if (!connector) {
		dpu_pr_err("connector_id=%u is not available!", params->dsi_id);
		return -1;
	}

	if (composer_active_vsync(connector->conn_info, true)) {
		dpu_pr_err("active vsync failed");
		return -1;
	}

	if (params->is_force_sync || params->trans_mode == MIPI_DSI_MODE_TRANS_SYNC) {
		mutex_lock(&connector->mipi_itf_sync_lock);
		ret = mipi_dsi_sync_write(connector, params);
		mutex_unlock(&connector->mipi_itf_sync_lock);
	} else if (params->trans_mode == MIPI_DSI_MODE_TRANS_ASYNC) {
		mutex_lock(&connector->mipi_itf_async_lock);
		ret = mipi_dsi_async_tx(connector, params);
		mutex_unlock(&connector->mipi_itf_async_lock);
	} else {
		dpu_pr_err("invalid trans mode");
	}

	composer_active_vsync(connector->conn_info, false);
	return ret;
}

int32_t dpu_mipi_dsi_rx(struct mipi_dsi_rx_params *params)
{
	int32_t ret = 0;
	struct dpu_connector *connector = NULL;

	if (mipi_dsi_rx_params_out_buffer_check(params) != 0) {
		return -1;
	}

	if (unlikely(params->read_cmd.dlen > BUF_MAX || params->read_cmd.dlen == 0)) {
		dpu_pr_err("dlen = %u is invalid", params->read_cmd.dlen);
		return -1;
	}

	if (unlikely(!params->read_cmd.payload)) {
		dpu_pr_err("payload is null");
		return -1;
	}

	connector = get_real_connector(params->dsi_id, params->panel_type);
	if (!connector) {
		dpu_pr_err("connector_id=%u is not available!", params->dsi_id);
		return -1;
	}
	dpu_pr_info("params->dsi_id %d, connector id %d ",params->dsi_id, connector->connector_id);

	if (composer_active_vsync(connector->conn_info, true)) {
		dpu_pr_err("active vsync failed");
		return -1;
	}

	mutex_lock(&connector->mipi_itf_sync_lock);
	ret = mipi_dsi_sync_read(connector, params);
	mutex_unlock(&connector->mipi_itf_sync_lock);

	composer_active_vsync(connector->conn_info, false);

	return ret;
}

static bool is_group_read_itf_support(struct dpu_connector *connector)
{
	struct dfr_info *dfr_info = dkmd_get_dfr_info(connector->conn_info);
	if (!dfr_info) {
		dpu_pr_warn("dfr_info is nullptr");
		return false;
	}

	if (dfr_info->dfr_mode != DFR_MODE_TE_SKIP_BY_MCU) {
		dpu_pr_err("group read is not supported in dfr mode %d", dfr_info->dfr_mode);
		return false;
	}
	return true;
}

int32_t dpu_mipi_dsi_group_rx(struct mipi_dsi_group_rx_params *params)
{
	int32_t ret = 0;
	struct dpu_connector *connector = NULL;

	// check whether params is valid
	if (mipi_dsi_group_rx_params_check(params) != 0)
		return MIPI_E_PARAMS_INVALID;

	connector = get_real_connector(params->dsi_id, params->panel_type);
	if (!connector) {
		dpu_pr_err("connector_id=%u is not available!", params->dsi_id);
		return MIPI_E_INVALID_PANEL;
	}

	if(!is_group_read_itf_support(connector))
		return MIPI_E_INTERFACE_NOT_SUPPORT;

	if (composer_active_vsync(connector->conn_info, true) != 0) {
		dpu_pr_err("active vsync failed");
		return MIPI_E_ACTIVE_VSYNC_FAILED;
	}

	mutex_lock(&connector->mipi_itf_sync_lock);
	ret = mipi_dsi_group_sync_read(connector, params);
	mutex_unlock(&connector->mipi_itf_sync_lock);

	composer_active_vsync(connector->conn_info, false);

	return ret;
}

int32_t dpu_dual_mipi_dsi_tx(struct mipi_dsi_tx_params *dsi0_params, struct mipi_dsi_tx_params *dsi1_params)
{
	int32_t ret = 0;
	struct dpu_connector *connector0 = NULL;
	struct dpu_connector *connector1 = NULL;

	if (mipi_dsi_tx_params_check(dsi0_params) != 0 || mipi_dsi_tx_params_check(dsi1_params) != 0) {
		return -1;
	}

	connector0 = get_real_connector(dsi0_params->dsi_id, dsi0_params->panel_type);
	if (unlikely(!connector0)) {
		dpu_pr_err("connector_id0=%u is not available!", dsi0_params->dsi_id);
		return -1;
	}

	connector1 = get_real_connector(dsi1_params->dsi_id, dsi1_params->panel_type);
	if (unlikely(!connector1)) {
		dpu_pr_err("connector_id1=%u is not available!", dsi1_params->dsi_id);
		return -1;
	}

	dpu_pr_info("params->dsi_id %d, connector id %d",dsi0_params->dsi_id, connector0->connector_id);
	dpu_pr_info("params->dsi_id %d, connector id %d",dsi1_params->dsi_id, connector1->connector_id);

	if (connector0->bind_connector != connector1) {
		dpu_pr_err("connector order error, pls exchange dsi0_params and dsi1_params");
		return -1;
	}

	if (composer_active_vsync(connector0->conn_info, true)) {
		dpu_pr_err("active vsync failed");
		return -1;
	}

	if (dsi0_params->is_force_sync || dsi0_params->trans_mode == MIPI_DSI_MODE_TRANS_SYNC) {
		mutex_lock(&connector0->mipi_itf_sync_lock);
		ret = dual_mipi_dsi_sync_write(connector0, connector1, dsi0_params, dsi1_params);
		mutex_unlock(&connector0->mipi_itf_sync_lock);
	} else if (dsi0_params->trans_mode == MIPI_DSI_MODE_TRANS_ASYNC) {
		mutex_lock(&connector0->mipi_itf_async_lock);
		ret = dual_mipi_dsi_async_tx(connector0, connector1, dsi0_params, dsi1_params);
		mutex_unlock(&connector0->mipi_itf_async_lock);
	} else {
		dpu_pr_err("invalid trans mode");
	}

	composer_active_vsync(connector0->conn_info, false);
	return ret;
}

int32_t dpu_dual_mipi_dsi_rx(struct mipi_dsi_rx_params *dsi0_params, struct mipi_dsi_rx_params *dsi1_params)
{
	int32_t ret = 0;
	struct dpu_connector *connector0 = NULL;
	struct dpu_connector *connector1 = NULL;
	dpu_pr_info("+");

	if (mipi_dsi_rx_params_out_buffer_check(dsi0_params) != 0 ||
		mipi_dsi_rx_params_out_buffer_check(dsi1_params) != 0) {
		return -1;
	}

	if (unlikely(dsi0_params->read_cmd.dlen > BUF_MAX || dsi0_params->read_cmd.dlen == 0)) {
		dpu_pr_err("dlen = %u is invalid.", dsi0_params->read_cmd.dlen);
		return -1;
	}

	if (unlikely(!dsi0_params->read_cmd.payload)) {
		dpu_pr_err("payload is null");
		return -1;
	}

	if (unlikely(dsi0_params->read_cmd.dlen != dsi1_params->read_cmd.dlen)) {
		dpu_pr_warn("dsi0 and dsi1 cmd is not same.");
	}

	connector0 = get_real_connector(dsi0_params->dsi_id, dsi0_params->panel_type);
	if (unlikely(!connector0)) {
		dpu_pr_err("connector_id0=%u is not available!", dsi0_params->dsi_id);
		return -1;
	}

	connector1 = get_real_connector(dsi1_params->dsi_id, dsi1_params->panel_type);
	if (unlikely(!connector1)) {
		dpu_pr_err("connector_id1=%u is not available!", dsi1_params->dsi_id);
		return -1;
	}

	dpu_pr_info("params->dsi_id %d, connector id %d",dsi0_params->dsi_id, connector0->connector_id);
	dpu_pr_info("params->dsi_id %d, connector id %d",dsi1_params->dsi_id, connector1->connector_id);

	if (connector0->bind_connector != connector1) {
		dpu_pr_err("connector order error, pls exchange dsi0_params and dsi1_params");
		return -1;
	}

	if (composer_active_vsync(connector0->conn_info, true)) {
		dpu_pr_err("active vsync failed");
		return -1;
	}
	mutex_lock(&connector0->mipi_itf_sync_lock);
	ret = dual_mipi_dsi_sync_read(connector0, connector1, dsi0_params, dsi1_params);
	mutex_unlock(&connector0->mipi_itf_sync_lock);

	composer_active_vsync(connector0->conn_info, false);
	return ret;
}

int32_t dpu_mipi_dsi_async_tx_stop(struct mipi_dsi_connector_params *con_params)
{
	int32_t ret = 0;
	struct dpu_connector *connector = NULL;

	if (!con_params) {
		dpu_pr_err("params is null");
		return MIPI_E_NULL_PTR;
	}

	connector = get_real_connector(con_params->dsi_id, con_params->panel_type);
	if (!connector) {
		dpu_pr_err("connector_id=%u is not available!", con_params->dsi_id);
		return MIPI_E_NULL_PTR;
	}

	if (composer_active_vsync(connector->conn_info, true)) {
		dpu_pr_err("active vsync failed");
		return -1;
	}

	mutex_lock(&connector->mipi_itf_async_lock);
	ret = mipi_dsi_async_tx_stop(connector);
	mutex_unlock(&connector->mipi_itf_async_lock);

	composer_active_vsync(connector->conn_info, false);
	return ret;
}

int32_t dpu_dual_mipi_dsi_group_rx(struct mipi_dsi_group_rx_params *dsi0_params, struct mipi_dsi_group_rx_params *dsi1_params)
{
	int32_t ret = 0;
    struct dpu_connector *connector0 = NULL;
    struct dpu_connector *connector1 = NULL;

	if (mipi_dsi_group_rx_params_check(dsi0_params) != 0 || mipi_dsi_group_rx_params_check(dsi1_params) != 0) {
        return MIPI_E_PARAMS_INVALID;
    }

	connector0 = get_real_connector(dsi0_params->dsi_id, dsi0_params->panel_type);
	if (unlikely(!connector0)) {
		dpu_pr_err("connector_id0=%u is not available!", dsi0_params->dsi_id);
		return MIPI_E_INVALID_PANEL;
	}

	connector1 = get_real_connector(dsi1_params->dsi_id, dsi1_params->panel_type);
	if (unlikely(!connector1)) {
		dpu_pr_err("connector_id1=%u is not available!", dsi1_params->dsi_id);
		return MIPI_E_INVALID_PANEL;
	}

	if (connector0->bind_connector != connector1) {
		dpu_pr_err("connector order error, pls exchange dsi0_params and dsi1_params");
		return MIPI_E_INVALID_PANEL;
	}

	if(!is_group_read_itf_support(connector0))
		return MIPI_E_INTERFACE_NOT_SUPPORT;

	if (composer_active_vsync(connector0->conn_info, true) != 0) {
		dpu_pr_err("active vsync failed");
		return MIPI_E_ACTIVE_VSYNC_FAILED;
	}

	mutex_lock(&connector0->mipi_itf_sync_lock);
	ret = dual_mipi_dsi_group_sync_read(connector0, connector1, dsi0_params, dsi1_params);
	mutex_unlock(&connector0->mipi_itf_sync_lock);

	composer_active_vsync(connector0->conn_info, false);

	return ret;
}