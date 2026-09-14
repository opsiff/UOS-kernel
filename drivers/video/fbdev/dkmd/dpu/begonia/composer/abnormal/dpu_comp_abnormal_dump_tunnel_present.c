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

#include "dpu_comp_mgr.h"
#include "dpu_comp_abnormal_handle.h"
#include "dpu_comp_tunnel_present.h"

#define MAX_TUNNEL_DISPLAY_COUNT 2 // tunnel channel only support 2 display, HDM constrain
#define INVALID_TUNNEL_ID (0xFFFFFFFF)

static inline char __iomem *get_layer_data_base_addr(struct dpu_composer *dpu_comp, uint32_t scene_id, uint32_t layer_number)
{
	char __iomem *base_addr = dpu_comp->comp_mgr->dpu_base;
	char __iomem *data_base_addr = NULL;

	if (scene_id == DPU_SCENE_ONLINE_0)
		data_base_addr = TUNNEL_DATA_PRIMARY_ADDR_BASE(base_addr);
	else
		data_base_addr = TUNNEL_DATA_EXTERNAL_ADDR_BASE(base_addr);

	return data_base_addr + layer_number * sizeof(struct slave_tunnel_data);
}

static void dump_tunnel_layer_data_reg(struct dpu_composer *dpu_comp, uint32_t scene_id, uint32_t layer_number)
{
	union tunnel_data_attr attr;
	char __iomem *layer_data_base_addr = get_layer_data_base_addr(dpu_comp, scene_id, layer_number);

	uint32_t tunnel_id_h = inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(tunnel_id_h));
	uint32_t tunnel_id_l = inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(tunnel_id_l));
	uint32_t buffer_h_addr = inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(buffer_h_addr));
	uint32_t buffer_l_addr = inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(buffer_l_addr));
	int32_t x = (int32_t)(inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(x)));
	int32_t y = (int32_t)(inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(y)));
	uint32_t property = inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(property));
	attr.value = inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(attr));
	uint32_t maintain = inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(maintain));
	uint32_t buffer_size = inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(buffer_size));

	dpu_pr_info("[tunnel] scene_id = %u tunnel_id = %u,%u buffer_h_addr = %u, buffer_l_addr = %u, "
		"x = %d y = %d property = %u data_index = %u update_enable = %u maintain = %u buffer_size %u",
		scene_id, tunnel_id_h, tunnel_id_l, buffer_h_addr, buffer_l_addr,
		x, y, property, attr.data_index, attr.update_enable, maintain, buffer_size);
}

static inline char __iomem * get_layer_info_base_addr(struct dpu_composer *dpu_comp, uint32_t scene_id)
{
	char __iomem *base_addr = dpu_comp->comp_mgr->dpu_base;

	if (scene_id == DPU_SCENE_ONLINE_0)
		return base_addr + TUNNEL_LAYER_INFO_PRIMARY_ADDR_BASE;

	return base_addr + TUNNEL_LAYER_INFO_EXTERNAL_ADDR_BASE;
}

static void dump_tunnel_layer_info_reg(struct dpu_composer *dpu_comp, uint32_t scene_id, uint32_t data_index)
{
	char __iomem *layer_info_base_addr = get_layer_info_base_addr(dpu_comp, scene_id);

	uint32_t tunnel_id_h = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, data_index, tunnel_id_h));
	uint32_t tunnel_id_l = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, data_index, tunnel_id_l));
	uint32_t layer_index = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, data_index, layer_index));
	uint32_t start_addr_h = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, data_index, start_addr_h));
	uint32_t start_addr_l = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, data_index, start_addr_l));
	uint32_t buffer_size = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, data_index, buffer_size));
	uint32_t bpp = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, data_index, bpp));
	uint32_t ov_height_clip = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, data_index, ov_height_clip));
	uint32_t property = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, data_index, property));
	uint32_t buffer_width = (uint32_t)(buffer_size & 0xFFFFU);
	uint32_t buffer_height = (uint32_t)(buffer_size >> 16U);

	dpu_pr_info("[tunnel] scene_id = %u tunnel_id = %u,%u layer_index = %u start_addr_h = %u start_addr_l = %u "
		"buffer_width = %u  buffer_height = %u bpp = %u ov_height_clip = %u property = %u",
		scene_id, tunnel_id_h, tunnel_id_l, layer_index, start_addr_h, start_addr_l, buffer_width, buffer_height, bpp,
		ov_height_clip, property);
}

void dpu_abnormal_dump_tunnel_present_dm(struct dpu_composer *dpu_comp)
{
	uint32_t scene_cnt = 0;
	uint32_t tunnel_layer_cnt = 0;

	if (unlikely(dpu_comp == NULL)) {
		dpu_pr_err("[tunnel] dpu_comp should not be null");
		return;
	}

	for (scene_cnt = 0; scene_cnt < MAX_TUNNEL_DISPLAY_COUNT; scene_cnt++) {
		for (tunnel_layer_cnt = 0; tunnel_layer_cnt < TUNNEL_LAYER_MAX; tunnel_layer_cnt++) {
			dump_tunnel_layer_data_reg(dpu_comp, scene_cnt, tunnel_layer_cnt);
			dump_tunnel_layer_info_reg(dpu_comp, scene_cnt, tunnel_layer_cnt);
		}
	}
}

static void dump_tunnel_state_reg(struct dpu_composer *dpu_comp, uint32_t scene_id, uint32_t layer_number)
{
	char __iomem *layer_data_base_addr = get_layer_data_base_addr(dpu_comp, scene_id, layer_number);
	char __iomem *layer_info_base_addr = get_layer_info_base_addr(dpu_comp, scene_id);

	uint32_t maintain = inp32(layer_data_base_addr + TUNNEL_LAYER_DATA_ITEM(maintain));
	uint32_t property = inp32(TUNNEL_LAYER_INFO_MEMBER_ADDR(layer_info_base_addr, layer_number, property));
	dpu_pr_debug("[tunnel] scene_id = %u maintain = 0x%08x property = 0x%08x", scene_id, maintain, property);

	if (maintain != 0) {
		if ((property != 0) && ((maintain & TUNNEL_PRESENT_END) != TUNNEL_PRESENT_END)) {
			dump_tunnel_layer_data_reg(dpu_comp, scene_id, layer_number);
			dump_tunnel_layer_info_reg(dpu_comp, scene_id, layer_number);
		}

		if (dpu_comp->cursor_stats.enable && ((property & TUNNEL_PROP_POSTION) == TUNNEL_PROP_POSTION))
			dpu_comp->cursor_stats.frame_count++;
	}
}

void dpu_check_tunnel_state(struct dpu_composer *dpu_comp)
{
	uint32_t scene_cnt = 0;
	uint32_t tunnel_layer_cnt = 0;

	if (unlikely(dpu_comp == NULL)) {
		dpu_pr_err("[tunnel] dpu_comp should not be null");
		return;
	}

	for (scene_cnt = 0; scene_cnt < MAX_TUNNEL_DISPLAY_COUNT; scene_cnt++) {
		for (tunnel_layer_cnt = 0; tunnel_layer_cnt < TUNNEL_LAYER_MAX; tunnel_layer_cnt++) {
			dump_tunnel_state_reg(dpu_comp, scene_cnt, tunnel_layer_cnt);
		}
	}
}