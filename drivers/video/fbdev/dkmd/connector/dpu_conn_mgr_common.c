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

#include "dpu_conn_mgr_common.h"
#include <linux/string.h>
#include "dvfs.h"

#define MAX_DP_POST_CH_INDEX 5
#define DP_POST_CH_FIVE_OFFSET 0x0100
#define PLUGOUT_INTER_VOTE_COUNT (3)
 
static bool is_connector_number_over(void)
{
	int connector_num = 0;
	bool ret = false;
	if (g_conn_manager->max_connector_num == 0) {
		return false;
	}

	mutex_lock(&g_conn_manager->connect_status_mutex);
	connector_num = count_connector_num();
	dpu_pr_info("connector_num is %d, max_connector_num is %d\n", connector_num, g_conn_manager->max_connector_num);
	ret = connector_num >= g_conn_manager->max_connector_num;
	mutex_unlock(&g_conn_manager->connect_status_mutex);
	return ret;
}

static int32_t notify_audio(struct dkmd_connector_info *pinfo, DPU_HOT_PLUG_TYPE hot_plug_type) {
	struct dpu_connector *connector = NULL;
	int32_t ret = 0;
	connector = get_primary_connector(pinfo);
	if (!connector) {
		dpu_pr_err("connector_id=%u is not available!\n", pinfo->connector_idx[PRIMARY_CONNECT_CHN_IDX]);
		return -EINVAL;
	}

	if (!pinfo->audio_switch.name || strlen(pinfo->audio_switch.name) == 0 || !connector->notify_audio) {
		dpu_pr_warn("connector_id=%u does not need notify audio:%s!\n", pinfo->connector_idx[PRIMARY_CONNECT_CHN_IDX],
			pinfo->audio_switch.name);
		return ret;
	}

	if (pinfo->base.comp_obj_info->is_plugin == hot_plug_type) {
		ret = connector->notify_audio(pinfo, hot_plug_type);
		dpu_pr_info("notify_audio start, hot_plug_type: 1, name:%s, ret:%d\n", pinfo->audio_switch.name, ret);
	}
	return ret;
}

static void connect_manager_revert_func(struct dkmd_connector_info *pinfo) {
	struct dpu_connector *connector = get_primary_connector(pinfo);
	if (!connector) {
		dpu_pr_err("connector_id=%u is not available!\n", pinfo->connector_idx[PRIMARY_CONNECT_CHN_IDX]);
		return;
	}

	connector->notify_hdm(pinfo, DPU_HOT_PLUG_OUT);
	connector->notify_audio(pinfo, DPU_HOT_PLUG_OUT);
	pinfo->base.comp_obj_info->is_plugin = pinfo->base.is_plugin = DPU_HOT_PLUG_OUT;
	if (connector->connect_revert_func)
		connector->connect_revert_func(pinfo);
}

int32_t connector_manager_connect(struct dkmd_connector_info *pinfo)
{
	int32_t ret = 0;
	struct dpu_connector *connector = NULL;

	if (!pinfo) {
		dpu_pr_err("pinfo is null!\n");
		return -EINVAL;
	}

	if (is_connector_number_over()) {
		return -EINVAL;
	}

	connector = get_primary_connector(pinfo);
	if (!connector) {
		dpu_pr_err("connector_id=%u is not available!\n", pinfo->connector_idx[PRIMARY_CONNECT_CHN_IDX]);
		return -EINVAL;
	}

	if (pinfo->base.comp_obj_info->is_plugin == DPU_HOT_PLUG_OUT) {
		dpu_pr_warn("connector_id=%u is plug out!\n", pinfo->connector_idx[PRIMARY_CONNECT_CHN_IDX]);
		return -EINVAL;
	}

	dpu_pr_info("connector_manager_connect start");
	if (connector->connect_func) {
		ret = connector->connect_func(pinfo);
		if (ret == 0) {
			mutex_lock(&g_conn_manager->connect_status_mutex);
			connector->is_connected = true;
			connector->has_ever_connected_after_plugin = true;
			mutex_unlock(&g_conn_manager->connect_status_mutex);
			ret = notify_audio(pinfo, DPU_HOT_PLUG_IN);
		} else {
			dpu_pr_warn("connector_id=%u connect failed!\n", pinfo->connector_idx[PRIMARY_CONNECT_CHN_IDX]);
			if (is_primary_panel(&pinfo->base)) {
				dpu_pr_warn("primary do not neet revert!\n");
				return ret;
			}

			if (!connector->has_ever_connected_after_plugin)
				connect_manager_revert_func(pinfo);
		}
	}
	return ret;
}

int32_t disconnect_post_handle(struct dkmd_connector_info *pinfo, char __iomem *dpu_base)
{
	int32_t ret = 0;
	struct dpu_connector *connector = NULL;

	if (!pinfo) {
		dpu_pr_err("pinfo is null!\n");
		return -EINVAL;
	}

	connector = get_primary_connector(pinfo);
	if (!connector) {
		dpu_pr_err("connector_id=%u is not available!\n", pinfo->connector_idx[PRIMARY_CONNECT_CHN_IDX]);
		return -EINVAL;
	}

	dpu_pr_info("disconnect_post_handle start");
	if (pinfo->base.comp_obj_info->is_plugin == DPU_HOT_PLUG_OUT && connector->disconnect_post_handle_func)
		ret = connector->disconnect_post_handle_func(pinfo, dpu_base);
	return ret;
}

int32_t connector_manager_disconnect(struct dkmd_connector_info *pinfo)
{
	int32_t ret = 0;
	struct dpu_connector *connector = NULL;

	if (!pinfo) {
		dpu_pr_err("pinfo is null!\n");
		return -EINVAL;
	}

	connector = get_primary_connector(pinfo);
	if (!connector) {
		dpu_pr_err("connector_id=%u is not available!\n", pinfo->connector_idx[PRIMARY_CONNECT_CHN_IDX]);
		return -EINVAL;
	}

	dpu_pr_info("connector_manager_disconnect start");
	if (connector->disconnect_func) {
		ret = connector->disconnect_func(pinfo);
		if (ret == 0) {
			mutex_lock(&g_conn_manager->connect_status_mutex);
			connector->is_connected = false;
			mutex_unlock(&g_conn_manager->connect_status_mutex);
			ret = notify_audio(pinfo, DPU_HOT_PLUG_OUT);
		} else {
			dpu_pr_warn("connector_id=%u disconnect failed!\n", pinfo->connector_idx[PRIMARY_CONNECT_CHN_IDX]);
		}
	}
	return ret;
}

int32_t register_switch_dev(struct dkmd_connector_info *pinfo)
{
	dpu_check_and_return(!pinfo, -EINVAL, err, "[DP] pinfo is null\n");
	if (switch_dev_register(&pinfo->video_switch) < 0)
		dpu_pr_err("video switch register failed!");

	if (switch_dev_register(&pinfo->audio_switch) < 0)
		dpu_pr_err("audio switch register failed!");
	return 0;
}
 
int32_t unregister_switch_dev(struct dkmd_connector_info *pinfo)
{
	dpu_check_and_return(!pinfo, -EINVAL, err, "[DP] pinfo is null\n");
	switch_dev_unregister(&pinfo->video_switch);
	switch_dev_unregister(&pinfo->audio_switch);
 
	return 0;
}

int32_t update_audio_dev(struct dkmd_connector_info *pinfo, char *audio_switch_name)
{
	dpu_pr_info("start update audio name:%s", pinfo->audio_switch.name);
	if (!audio_switch_name) {
		dpu_pr_warn("update audio name fail");
		return -1;
	}

	pinfo->audio_switch.name = audio_switch_name;
	return 0;
}

int32_t connector_trigger_debug(int connector_idx, DPU_HOT_PLUG_TYPE hot_plug_type)
{
	struct dkmd_connector_info *pinfo = NULL;
	struct dpu_connector *connector_temp = NULL;
	int ret = 1;
	connector_temp = get_connector_by_id(connector_idx);
	pinfo = connector_temp->conn_info;
	ret = connector_device_trigger_uevent_hotplug(pinfo, hot_plug_type);
	return ret;
}

static void set_inter_vote_index_count(DPU_HOT_PLUG_TYPE hot_plug_type) {
	switch(hot_plug_type) {
		case DPU_HOT_PLUG_IN:
			dpu_dvfs_increase_count_locked();
			break;
		case DPU_HOT_PLUG_OUT:
			dpu_dvfs_set_inter_vote_index_count(PLUGOUT_INTER_VOTE_COUNT);
			break;
		default:
			dpu_pr_warn("invalid hot_plug_type=%d", hot_plug_type);
	}
}

int32_t connector_device_trigger_uevent_hotplug(struct dkmd_connector_info *pinfo, DPU_HOT_PLUG_TYPE hot_plug_type)
{
	int notify_audio_ret = 0;
	struct dpu_connector *connector = NULL;
	if (!pinfo) {
		dpu_pr_err("pinfo is NULL!\n");
		return -1;
	}

	connector = get_primary_connector(pinfo);
	if (!connector) {
		dpu_pr_err("connector is not available!\n");
		return -1;
	}

	dpu_pr_info("HPD Type: %d, Connector id : %u", hot_plug_type, connector->connector_id);
	if (hot_plug_type >= DPU_HOT_PLUG_MAX_NUM) {
		dpu_pr_err("hot_plug_type=%u is not of range", hot_plug_type);
		return -1;
	}

	// check present status
	if (pinfo->base.comp_obj_info->is_plugin == hot_plug_type) {
		dpu_pr_err("connector%u presence status has been %d !\n", connector->connector_id, hot_plug_type);
		return 0;
	}

	// update present status
	pinfo->base.comp_obj_info->is_plugin = pinfo->base.is_plugin = hot_plug_type;
	set_inter_vote_index_count(hot_plug_type);
	connector->has_ever_connected_after_plugin = false;
	if (hot_plug_type == DPU_HOT_PLUG_OUT) {
		notify_audio_ret = connector->notify_audio(pinfo, DPU_HOT_PLUG_OUT);
		if (notify_audio_ret != 0) {
			dpu_pr_err("connector %u notify audio failed, ret: %d", connector->connector_id, notify_audio_ret);
		}
	}
	return connector->notify_hdm(pinfo, hot_plug_type);
}

int clear_pip_sw_config(char __iomem *dpu_base, struct dkmd_connector_info *pinfo, int post_chn)
{
	uint32_t primary_post_ch_offset = (uint32_t)post_chn * 0x1C;
	dpu_pr_info("clear_pip_sw_config");

	if (composer_active_vsync(pinfo, true)) {
		dpu_pr_err("active vsync failed");
		return -1;
	}

	if (post_chn == MAX_DP_POST_CH_INDEX)
		primary_post_ch_offset = DP_POST_CH_FIVE_OFFSET;
	dpu_pr_info("[DP] dpu_dptx_handle_plugout_chandle_sceneid primary_post_ch_offset is %#x", primary_post_ch_offset);

	set_reg(DPU_PIPE_SW_SIG_CTRL_0_ADDR(dpu_base + DPU_PIPE_SW_OFFSET) \
		+ primary_post_ch_offset, 0x0, 32, 0);
	set_reg(DPU_PIPE_SW_SW_POS_CTRL_SIG_EN_0_ADDR(dpu_base + DPU_PIPE_SW_OFFSET) \
		+ primary_post_ch_offset, 0x0, 32, 0);
	set_reg(DPU_PIPE_SW_DAT_CTRL_0_ADDR(dpu_base + DPU_PIPE_SW_OFFSET) \
		+ primary_post_ch_offset, 0x0, 32, 0);
	set_reg(DPU_PIPE_SW_SW_POS_CTRL_DAT_EN_0_ADDR(dpu_base + DPU_PIPE_SW_OFFSET) \
		+ primary_post_ch_offset, 0x0, 32, 0);
	composer_active_vsync(pinfo, false);

	return 0;
}

#ifdef CONFIG_DKMD_DEBUG_ENABLE
EXPORT_SYMBOL(connector_trigger_debug);
#endif