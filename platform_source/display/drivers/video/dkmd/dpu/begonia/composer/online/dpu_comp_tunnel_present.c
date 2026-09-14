/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
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
#include "dpu_comp_tunnel_present.h"

#include <linux/delay.h>
#include "dvfs.h"
#include "dp_drv.h"
#include "dksm_debug.h"
#include "dpu_comp_config_utils.h"
#include "dpu_comp_dfr_te_skip_acpu.h"
#include "dp_ctrl_dev.h"
#include "dpu_dev_present_vote.h"

#define INVALID_TUNNEL_ID (0xFFFFFFFF)

/* tunnel present start */
static inline bool is_online_present(struct dpu_composer *dpu_comp) 
{
	return !is_offline_panel(&dpu_comp->conn_info->base);
}

static int32_t get_tunnel_online_scene_id(struct dpu_composer *dpu_comp)
{
    int32_t scene_id;
    struct comp_online_present *present = NULL;
    dpu_check_and_return(!is_online_present(dpu_comp), -1, err, "[tunnel] not support offline");

    present = (struct comp_online_present *)dpu_comp->present_data;
    scene_id = present->frames[present->displaying_idx].in_frame.scene_id;
    dpu_pr_debug("[tunnel] scene_id = %d", scene_id);
    return scene_id;
}

static bool has_tunnel_layer(struct dpu_composer *dpu_comp)
{
    bool has_tunnel_layer = false;
    struct comp_online_present *present = NULL;

	if (!is_online_present(dpu_comp)) {
        dpu_pr_err("[tunnel] not support offline");
		return false;
    }

    present = (struct comp_online_present *)dpu_comp->present_data;
    has_tunnel_layer = present->frames[present->displaying_idx].in_frame.tunnel_info.has_tunnel_layer;
    dpu_pr_debug("[tunnel] has_tunnel_layer = %d", has_tunnel_layer);
    return has_tunnel_layer;
}

static inline char __iomem *get_tunnel_base(struct dpu_composer *dpu_comp, uint32_t data_index)
{
    char __iomem *layer_base_addr = NULL;
    char __iomem *tunnel_base_addr = NULL;
    char __iomem *base_addr = NULL;

    int32_t scene_id = get_tunnel_online_scene_id(dpu_comp);
    if (scene_id < 0) {
        dpu_pr_err("[tunnel] scene_id = %d", scene_id);
        return NULL;
    }
    base_addr = dpu_comp->comp_mgr->dpu_base;

    if (scene_id == DPU_SCENE_ONLINE_0) {
        layer_base_addr = TUNNEL_DATA_PRIMARY_ADDR_BASE(base_addr);
    } else {
        layer_base_addr = TUNNEL_DATA_EXTERNAL_ADDR_BASE(base_addr);
    }
    tunnel_base_addr = layer_base_addr + data_index * sizeof(struct slave_tunnel_data);
    return tunnel_base_addr;
}

static void set_tunnel_data_regs(struct dpu_composer *dpu_comp, struct tunnel_layer_data *data)
{
    uint32_t tunnel_id_h;
    uint32_t tunnel_id_l;
    union tunnel_data_attr attr = {0};
    char __iomem *base_addr = NULL;
    uint32_t data_index = data->data_index;
    uint32_t buffer_size = 0;

    if (data_index >= TUNNEL_LAYER_MAX) {
        dpu_pr_err("[tunnel] data_index error data_index = %u, max_layer_index = %u", data_index, TUNNEL_LAYER_MAX);
        return;
    }
    base_addr = get_tunnel_base(dpu_comp, data_index);
    dpu_check_and_no_retval(!base_addr, err, "base_addr is null");

    tunnel_id_h = (uint32_t)(data->tunnel_id >> 32); /* 32 bit */
    outp32(base_addr + TUNNEL_LAYER_DATA_ITEM(tunnel_id_h), tunnel_id_h);
    tunnel_id_l = (uint32_t)(data->tunnel_id & 0xFFFFFFFF);
    outp32(base_addr + TUNNEL_LAYER_DATA_ITEM(tunnel_id_l), tunnel_id_l);
    if (tunnel_id_h == INVALID_TUNNEL_ID && tunnel_id_l == INVALID_TUNNEL_ID)
        data->data_index = 0;

    outp32(base_addr + TUNNEL_LAYER_DATA_ITEM(buffer_h_addr), data->buffer_h_addr);
    outp32(base_addr + TUNNEL_LAYER_DATA_ITEM(buffer_l_addr), data->buffer_l_addr);
    outp32(base_addr + TUNNEL_LAYER_DATA_ITEM(x), data->x);
    outp32(base_addr + TUNNEL_LAYER_DATA_ITEM(y), data->y);
    outp32(base_addr + TUNNEL_LAYER_DATA_ITEM(property), data->property);

    attr.data_index = data->data_index;
    attr.update_enable = has_tunnel_layer(dpu_comp) ? 1 : 0;

    outp32(base_addr + TUNNEL_LAYER_DATA_ITEM(attr), attr.value);
    outp32(base_addr + TUNNEL_LAYER_DATA_ITEM(buffer_size), buffer_size);

    dpu_pr_debug("[tunnel] tunnel_id  = %u,%u addr_h = %u, addr_l = %u, x = %d, y = %d, property = %u, "
        "data_index = %u, update_enable = %u, buffer_size = %u",
        tunnel_id_h, tunnel_id_l, data->buffer_h_addr, data->buffer_l_addr,
        data->x, data->y, data->property, attr.data_index, attr.update_enable, buffer_size);
}

void dpu_tunnel_clear_position_data(struct dpu_composer *dpu_comp)
{
    uint32_t tunnel_layer_cnt = 0;
    struct tunnel_layer_data layer_data = {0};
    uint64_t tunnel_id = 0;
    uint32_t tunnel_id_h = 0;
    uint32_t tunnel_id_l = 0;
    uint32_t property = 0;
    dpu_check_and_no_retval(!dpu_comp, err, "dpu_comp is null");
    dpu_check_and_no_retval(!is_online_present(dpu_comp), err, "[tunnel] not support offline");

    for (tunnel_layer_cnt = 0; tunnel_layer_cnt < TUNNEL_LAYER_MAX; tunnel_layer_cnt++) {
        tunnel_id = dpu_comp->data_ctrl_arr[tunnel_layer_cnt].data.tunnel_id;
        tunnel_id_h = (uint32_t)(tunnel_id >> 32); /* 32 bit */
        tunnel_id_l = (uint32_t)(tunnel_id & 0xFFFFFFFF);
        property = dpu_comp->data_ctrl_arr[tunnel_layer_cnt].data.property;
        if ((tunnel_id_h == INVALID_TUNNEL_ID && tunnel_id_l == INVALID_TUNNEL_ID) ||
            (tunnel_id_h == 0 && tunnel_id_l == 0) ||
            ((property & TUNNEL_PROP_POSTION) != TUNNEL_PROP_POSTION)) {
            continue;
        }
        set_tunnel_data_regs(dpu_comp, &layer_data);
        dpu_pr_debug("[tunnel] tunnel_layer_cnt = %u clear tunnel data regs", tunnel_layer_cnt);
    }
}

void dpu_tunnel_set_position_data(struct dpu_composer *dpu_comp)
{
    uint32_t tunnel_layer_cnt = 0;
    struct tunnel_layer_data layer_data = {0};
    uint32_t tunnel_id_h = 0;
    uint32_t tunnel_id_l = 0;
    dpu_check_and_no_retval(!dpu_comp, err, "dpu_comp is null");
    dpu_check_and_no_retval(!is_online_present(dpu_comp), err, "[tunnel] not support offline");

    for (tunnel_layer_cnt = 0; tunnel_layer_cnt < TUNNEL_LAYER_MAX; tunnel_layer_cnt++) {
        layer_data.tunnel_id = dpu_comp->data_ctrl_arr[tunnel_layer_cnt].data.tunnel_id;
        tunnel_id_h = (uint32_t)(layer_data.tunnel_id >> 32); /* 32 bit */
        tunnel_id_l = (uint32_t)(layer_data.tunnel_id & 0xFFFFFFFF);
        layer_data.property = dpu_comp->data_ctrl_arr[tunnel_layer_cnt].data.property;
        if ((tunnel_id_h == INVALID_TUNNEL_ID && tunnel_id_l == INVALID_TUNNEL_ID) ||
            (tunnel_id_h == 0 && tunnel_id_l == 0) ||
            ((layer_data.property & TUNNEL_PROP_POSTION) != TUNNEL_PROP_POSTION)) {
            continue;
        }
        layer_data.x = dpu_comp->data_ctrl_arr[tunnel_layer_cnt].data.x;
        layer_data.y = dpu_comp->data_ctrl_arr[tunnel_layer_cnt].data.y;
        layer_data.data_index = dpu_comp->data_ctrl_arr[tunnel_layer_cnt].data.data_index;
        set_tunnel_data_regs(dpu_comp, &layer_data);
        dpu_pr_debug("[tunnel] tunnel_layer_cnt = %u set tunnel coord regs", tunnel_layer_cnt);
    }
}

int32_t dpu_tunnel_data_config_by_te_isr(struct dpu_composer *dpu_comp, struct tunnel_data_ctrl *data_ctrl)
{
    int32_t ret = 0;
    unsigned long flags = 0;
    struct tunnel_layer_data *data = NULL;

    dpu_pr_debug("[tunnel] dpu_tunnel_data_config_by_te_isr");

    if (unlikely(dpu_comp == NULL) || unlikely(data_ctrl == NULL)) {
        dpu_pr_err("tunnel dpu_comp is null or data_ctrl is null");
        return -1;
    }
    dpu_check_and_return(!is_online_present(dpu_comp), -1, err, "[tunnel] not support offline");

    ret = increase_present_vote(dpu_comp);
    if (ret != 0) {
        dpu_pr_err("tunnel increase_present_vote fail, vote_ret %d", ret);
        return -1;
    }

    spin_lock_irqsave(&(data_ctrl->lock), flags);
    data = &(data_ctrl->data);
    data_ctrl->update = false;
    set_tunnel_data_regs(dpu_comp, data);
    spin_unlock_irqrestore(&(data_ctrl->lock), flags);

    ret = decrease_present_vote();
    if (ret != 0) {
        dpu_pr_err("tunnel decrease_present_vote fail, vote_ret = %d", ret);
    }
    return ret;
}

static int32_t update_tunnel_data_ctrl_arr(struct dpu_composer *dpu_comp,
    struct tunnel_layer_data *data, bool is_acpu_timing_ctrl)
{
    unsigned long flags = 0;
    uint32_t data_index = data->data_index;
    struct tunnel_data_ctrl *data_ctrl = NULL;

    if (data_index >= TUNNEL_LAYER_MAX) {
        dpu_pr_err("[tunnel] data_index error data_index = %u, max_layer_index = %u", data_index, TUNNEL_LAYER_MAX);
        return -1;
    }

    data_ctrl = &(dpu_comp->data_ctrl_arr[data_index]);
    spin_lock_irqsave(&(data_ctrl->lock), flags);
    if (memcpy_s(&(data_ctrl->data), sizeof(struct tunnel_layer_data),
        data, sizeof(struct tunnel_layer_data)) != EOK) {
        dpu_pr_err("[tunnel] memcpy_s failed");
        spin_unlock_irqrestore(&(data_ctrl->lock), flags);
        return -1;
    }

    if (is_acpu_timing_ctrl)
        data_ctrl->update = true;
    spin_unlock_irqrestore(&(data_ctrl->lock), flags);
    return 0;
}

static int32_t dpu_tunnel_data_config_by_acpu(struct dpu_composer *dpu_comp, struct tunnel_layer_data *data)
{
    struct dpu_multi_present_ctrl *multi_present_ctrl = &dpu_comp->multi_present_ctrl;
    dpu_check_and_return(!multi_present_ctrl->ops, -1, err, "[tunnel] ops is null");
    dpu_check_and_return(!multi_present_ctrl->ops->set_present_request, -1, err, "[tunnel] set_present_request is null");

    dpu_pr_debug("[tunnel] dpu_tunnel_data_config_by_acpu");

    if (update_tunnel_data_ctrl_arr(dpu_comp, data, true) != 0) {
        dpu_pr_err("[tunnel] update_tunnel_data_ctrl_arr fail");
        return -1;
    }

    dpu_comp_active_vsync(dpu_comp);
    multi_present_ctrl->ops->set_present_request(multi_present_ctrl, PRESENT_CLIENT_TUNNEL);
    dpu_comp_deactive_vsync(dpu_comp);

    return 0;
}
 
static void tunnel_present_set_dvfs_vote(struct dpu_composer* dpu_comp)
{
    struct comp_online_present *present = NULL;
    struct disp_frame *frame = NULL;
    struct dkmd_connector_info *pinfo = dpu_comp->conn_info;
	dpu_check_and_no_retval(!pinfo, err, "pinfo is null pointer");
    dpu_check_and_no_retval(((pinfo->vsync_ctrl_type & VSYNC_IDLE_MIPI_ULPS) == 0), debug, "[tunnel] ulps not support");
    dpu_check_and_no_retval(is_offline_panel(&pinfo->base), err, "[tunnel] is_offline_panel");
 
    present = (struct comp_online_present *)dpu_comp->present_data;
    frame = &present->frames[present->displaying_idx].in_frame;
 
    if (frame->tunnel_info.has_tunnel_layer)
        dpu_dvfs_intra_frame_vote(present->dpu_comp->comp.index, &frame->dvfs_info, false);
}

static int32_t dpu_tunnel_data_config_by_mcu(struct dpu_composer *dpu_comp, struct tunnel_layer_data *data)
{
    dpu_pr_debug("[tunnel] dpu_tunnel_data_config_by_mcu");

    if (update_tunnel_data_ctrl_arr(dpu_comp, data, false) != 0) {
        dpu_pr_err("[tunnel] update_tunnel_data_ctrl_arr fail");
        return -1;
    }

    if (increase_present_vote(dpu_comp) != 0) {
        dpu_pr_warn("[tunnel] increase_present_vote fail");
        return 1;
    }
    dpu_comp_active_vsync(dpu_comp);
    tunnel_present_set_dvfs_vote(dpu_comp);
    set_tunnel_data_regs(dpu_comp, data);
    dpu_comp_deactive_vsync(dpu_comp);
    if (decrease_present_vote() != 0)
        dpu_pr_err("[tunnel] decrease_present_vote fail");

    return 0;
}

static inline bool is_acpu_timing_ctrl(struct dpu_composer *dpu_comp)
{
    return is_edp_cmd_mode(dpu_comp->conn_info);
}

static int32_t dpu_tunnel_data_config(struct dpu_composer *dpu_comp, struct tunnel_layer_data *data)
{
    int32_t ret = 0;

    if (is_acpu_timing_ctrl(dpu_comp)) {
        ret = dpu_tunnel_data_config_by_acpu(dpu_comp, data);
        if (ret < 0) {
            dpu_pr_err("[tunnel] dpu_tunnel_data_config_by_acpu failed");
            return -1;
        }
        return ret;
    }

    ret = dpu_tunnel_data_config_by_mcu(dpu_comp, data);
    if (ret < 0) {
        dpu_pr_err("[tunnel] dpu_tunnel_data_config_by_mcu failed");
        return -1;
    }
    return ret;
}

int32_t dpu_tunnel_present(struct composer *comp, struct tunnel_layer_data *data)
{
    int32_t ret = 0;
    struct dpu_composer *dpu_comp = NULL;

    dpu_pr_debug("[tunnel] gfx_tunnel_present");
    dpu_comp = to_dpu_composer(comp);
    if (unlikely(dpu_comp == NULL)) {
        dpu_pr_err("[tunnel] dpu_comp should not be null");
        return -1;
    }

    if (unlikely(data == NULL)) {
        dpu_pr_err("[tunnel] data should not be null");
        return -1;
    }

    if (!is_online_present(dpu_comp))
        return 0;

    ret = dpu_tunnel_data_config(dpu_comp, data);
    if (ret < 0) {
        dpu_pr_err("[tunnel] dpu_tunnel_data_config fail");
        return -1;
    }
    return ret;
}
/* tunnel present end */

/* hardware cursor status count start */
int32_t dpu_tunnel_enable_hardware_cursor_stats(struct composer *comp, int32_t scene_id, bool enable)
{
    struct dpu_composer *dpu_comp = NULL;
    dpu_pr_debug("[hw cursor] hardware cursor stats %sable on scene %d", (enable) ? ("en") : ("dis"), scene_id);

    dpu_comp = to_dpu_composer(comp);

    if (enable) {
        dpu_comp->cursor_stats.vsync_count = dpu_comp->vsync_count;
        dpu_comp->cursor_stats.frame_count = 0;
        dpu_comp->cursor_stats.enable = true;
    } else {
        dpu_comp->cursor_stats.vsync_count = dpu_comp->vsync_count - dpu_comp->cursor_stats.vsync_count;
        dpu_comp->cursor_stats.enable = false;
    }

    return 0;
}

int32_t dpu_tunnel_get_hardware_cursor_stats(
	struct composer *comp, int32_t scene_id, uint32_t *frame_count, uint32_t *vsync_count)
{
    struct dpu_composer *dpu_comp = to_dpu_composer(comp);
    (*frame_count) = dpu_comp->cursor_stats.frame_count;
    (*vsync_count) = (uint32_t)dpu_comp->cursor_stats.vsync_count;

    dpu_pr_debug("[hw cursor] hardware cursor stats get on scene_id=%d, frame_count=%d, vsync_count=%d",
        scene_id, (*frame_count), (*vsync_count));

    return 0;
}
/* hardware cursor status count end */