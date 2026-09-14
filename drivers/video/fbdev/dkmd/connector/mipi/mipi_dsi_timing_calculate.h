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

#ifndef __MIPI_DSI_TIMING_CALCULATE_H__
#define __MIPI_DSI_TIMING_CALCULATE_H__

#include <linux/types.h>
#include "dkmd_object.h"
#include "dkmd_connector.h"
#include "dpu_connector.h"

#ifndef round1
#define round1(x, y)  ((x) / (y) + (((x) % (y) != 0) ? 1 : 0))
#endif

#define POW2(n) (1 << (n))
#define FPS_MULTIPLES 2400000
#define REF_CLK_BASE 192
#define RG_PLL_PREDIV 1
#define LANE_NUMS_UNIT 4
#define MIN_HFP_TIME 600
#define BYTE_SIZE 8
#define CPHY_BYTE_PER_CYCLE 2
#define DPHY_BYTE_PER_CYCLE 1
#define DPHY_DSI_LANEBYTECLK_UNIT 8
#define CPHY_DSI_LANEBYTECLK_UNIT 7
#define CPHY_PACKET_HEADER_SIZE 30
#define DPHY_PACKET_HEADER_SIZE 6
#define HLINE_TIME_PER_ADD 60
#define MAX_RG_PLL_POS 5
#define MIN_RG_PLL_POS 0
#define MAX_RG_PLL_FBK 209
#define MIN_RG_PLL_FBK 104
#define COMPRESS_RATIO 1

struct cdphy_pll_paras {
	uint64_t min_pos;
	uint64_t min_fbk;
};

struct mipi_config_param {
	uint64_t dsi_lane_byteclk_unit;
	uint64_t packet_header_size;
	uint64_t byte_per_cycle;
	uint64_t lane_nums;
	uint64_t compress_ratio;
	uint64_t bpp;
};

int mipi_dsi_param_calculate(struct dkmd_connector_info *pinfo, struct dpu_connector *connector, struct panel_timing_cfg timing);

#endif