/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __EDID_TIMING_HANDLE_H__
#define __EDID_TIMING_HANDLE_H__

#include "dkmd_connector.h"

extern struct edid_hook_ops *g_edid_hook_ops;

#ifndef VALUE_DIFF
#define VALUE_DIFF(a, b)	(((a) >= (b)) ? ((a) - (b)) : ((b) - (a)))
#endif

#ifndef IMAGE_RATIO
#define IMAGE_RATIO(a, b)	(((a) * 100) / (b))
#endif

#define EDID_DEFAULT_PREFERRED_TIMING_INDEX	0

/* edid handle for the product specification */
struct edid_hook_ops {
	int (*save_edid)(uint8_t *edid_buf, uint32_t buf_len, uint8_t port_id);
	bool (*is_valid_timing_info)(uint16_t hactive_pixels, uint16_t vactive_pixels, uint32_t fps, uint8_t port_id);
};

void dpu_edid_hook_register(struct edid_hook_ops *ops);
int dpu_conn_edid_timing_filter(struct dkmd_connector_info *pinfo, uint8_t *edid);
bool edid_check_timing_preferred(struct dkmd_connector_info *pinfo, struct edid_timing_info *cur_timing_info,
	struct edid_timing_info *pref_timing_info);
struct edid_timing_info *edid_get_preferred_timing(struct dkmd_connector_info *pinfo);
void edid_clear_invalid_timing(struct edid_timing_info *timing_list, uint32_t start_index, uint32_t end_index);
void edid_place_preferred_timing_at_array_first(struct edid_timing_info *timing_list,
	struct edid_timing_info **pref_timing_info, uint32_t pref_timing_id);

#endif