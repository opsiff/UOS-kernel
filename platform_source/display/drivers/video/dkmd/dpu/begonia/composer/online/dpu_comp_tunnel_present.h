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
 
#ifndef DPU_COMP_TUNNEL_PRESENT_H
#define DPU_COMP_TUNNEL_PRESENT_H
 
#include <linux/types.h>
#include "dkmd_dpu.h"
#include "dpu_comp_mgr.h"

enum tunnel_layer_property {
    TUNNEL_PROP_INVALID = 0,
    TUNNEL_PROP_POSTION = BIT(0),
    TUNNEL_PROP_BUFFER_ADDR = BIT(1),
    TUNNEL_PROP_CLIENT_COMMIT = BIT(2),
    TUNNEL_PROP_DEVICE_COMMIT = BIT(3),
    TUNNEL_PROP_SECURE_DEVICE_COMMIT = BIT(4),
};

enum tunnel_maintain {
    GET_TUNNEL_LAYER_DATA = BIT(0),
    GET_TUNNEL_LAYER_INFO = BIT(1),
    CHECK_PARAM_INVALID = BIT(2),
    GET_DM_LAYER_INFO = BIT(3),
    SET_TUNNEL_LAYER_REGS = BIT(4),
    MERGE_Y_PAYLOAD = BIT(5),
    MERGE_C_PAYLOAD = BIT(6),
    MERGE_Y_HEADDER = BIT(7),
    MERGE_C_HEADDER = BIT(8),
    TUNNEL_PRESENT_END = BIT(9),
};

#define MAX_TUNNEL_DISPLAY_COUNT 2 // tunnel channel only support 2 display, HDM constrain

#ifdef CONFIG_DKMD_DPU_TUNNEL_PRESENT
void dpu_tunnel_clear_position_data(struct dpu_composer *dpu_comp);
void dpu_tunnel_set_position_data(struct dpu_composer *dpu_comp);
int32_t dpu_tunnel_data_config_by_te_isr(struct dpu_composer *dpu_comp, struct tunnel_data_ctrl *data_ctrl);
int32_t dpu_tunnel_present(struct composer *comp, struct tunnel_layer_data *data);
int32_t dpu_tunnel_enable_hardware_cursor_stats(struct composer *comp, int32_t scene_id, bool enable);
int32_t dpu_tunnel_get_hardware_cursor_stats(
    struct composer *comp, int32_t scene_id, uint32_t *frame_count, uint32_t *vsync_count);
#else
#define dpu_tunnel_clear_position_data(dpu_comp)
#define dpu_tunnel_set_position_data(dpu_comp)
#define dpu_tunnel_data_config_by_te_isr(dpu_comp, data_ctrl) (0)
#define dpu_tunnel_present(comp, data) (0)
#define dpu_tunnel_enable_hardware_cursor_stats(comp, scene_id, enable) (0)
#define dpu_tunnel_get_hardware_cursor_stats(comp, scene_id, frame_count, vsync_count) (0)
#endif
#endif