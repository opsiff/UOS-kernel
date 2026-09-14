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

#include <linux/errno.h>

#include "mipi_dsi_timing_calculate.h"
#include "dkmd_log.h"
#include "dkmd_mipi_panel_info.h"
#include "mipi_cdphy_utils.h"

static uint64_t pixel_to_cycle(uint64_t pixel, bool is_add_packet_header, const struct mipi_config_param mipi_cfg_param)
{
	uint64_t cycle;
	int bpp = mipi_cfg_param.bpp;
	int packet_header_size = mipi_cfg_param.packet_header_size;
	int byte_per_cycle = mipi_cfg_param.byte_per_cycle;
	int lane_nums = mipi_cfg_param.lane_nums;

	if (is_add_packet_header)
		cycle = round1(((pixel * (uint64_t)bpp / COMPRESS_RATIO / BYTE_SIZE) + (uint64_t)packet_header_size), ((uint64_t)byte_per_cycle * (uint64_t)lane_nums));
	else
		cycle = (pixel * (uint64_t)bpp / COMPRESS_RATIO / BYTE_SIZE) / ((uint64_t)byte_per_cycle * (uint64_t)lane_nums);

	return cycle;
}

static uint64_t calculate_dsi_bit_clk(uint64_t rg_pll_fbk, uint64_t rg_pll_pos)
{
	uint64_t ref_clk = REF_CLK_BASE * POW2(RG_PLL_PREDIV);
	return round1((rg_pll_fbk * ref_clk), (2 * POW2(rg_pll_pos) * 10));
}

// cal method: (1000 / (timing.vtotal * mipi->hline_time / dsi_lanebyteclk / 1000));
static uint64_t calculate_fps(uint64_t rg_pll_fbk, uint64_t rg_pll_pos, struct mipi_panel_info *mipi, struct panel_timing_cfg timing)
{
	uint64_t hline_vtotal = mipi->hline_time * timing.vtotal;
	uint64_t fps = FPS_MULTIPLES * rg_pll_fbk / POW2(rg_pll_pos) / hline_vtotal;
	uint64_t fps_remainder = ((uint64_t)((FPS_MULTIPLES / POW2(rg_pll_pos) * rg_pll_fbk) % (hline_vtotal)) * 1000) / (hline_vtotal);
	fps = fps * 1000 + fps_remainder;
	return fps;
}

static int calculate_mipi_clk(struct mipi_panel_info *mipi, struct panel_timing_cfg timing)
{
	uint64_t rg_pll_fbk, rg_pll_pos, fps, fps_diff, min_fbk, min_pos = 0;
	uint64_t min_fps_diff = 1000000;
	uint64_t frame_fps = (timing.clock * 1000 * 1000) / (timing.vtotal * timing.htotal);

	for (rg_pll_pos = MAX_RG_PLL_POS; rg_pll_pos >= MIN_RG_PLL_POS; rg_pll_pos--) {
		min_pos = rg_pll_pos;

		for (rg_pll_fbk = MIN_RG_PLL_FBK; rg_pll_fbk < MAX_RG_PLL_FBK; rg_pll_fbk++) {
			fps = calculate_fps(rg_pll_fbk, rg_pll_pos, mipi, timing);
			fps_diff = fps > frame_fps ? (fps - frame_fps) : (frame_fps - fps);
			min_fbk = rg_pll_fbk;
			if (fps_diff <= min_fps_diff) {
				min_fps_diff = fps_diff;
			} else {
				mipi->dsi_bit_clk = (uint32_t)(calculate_dsi_bit_clk(min_fbk, min_pos) / 2);
				mipi->dsi_bit_clk_upt = mipi->dsi_bit_clk;
				mipi->dsi_bit_clk_default = mipi->dsi_bit_clk;
				mipi->pxl_clk_rate = (frame_fps / 1000) * timing.vtotal * (mipi->hline_time * BYTE_SIZE / 3);
				dpu_pr_info("pxl clk rate=%u", mipi->pxl_clk_rate);
				dpu_pr_info("min_pos=%llu, min_fbk=%llu, dsi_bit_clk=%u", min_pos, min_fbk, mipi->dsi_bit_clk);
				return 0;
			}
		}
	}
	return -EINVAL;
}

static int set_mipi_dsi_timing_param(struct mipi_panel_info *mipi, struct panel_timing_cfg timing, const struct mipi_config_param mipi_cfg_param)
{
	int ret = 0;
	int hfp, hfp_time = 0;

	mipi->hsa = pixel_to_cycle(timing.hsa, false, mipi_cfg_param);
	mipi->hbp = pixel_to_cycle(timing.hbp, false, mipi_cfg_param);
	mipi->vsa = timing.vsa;
	mipi->vbp = timing.vbp;
	mipi->vfp = timing.vfp;
	mipi->vactive_line = timing.vactive;
	mipi->dpi_hsize = pixel_to_cycle(timing.hactive, true, mipi_cfg_param);
	if (mipi->dpi_hsize % 2 != 0)
		mipi->dpi_hsize += 1;

	// init hline time expand 50
	mipi->hline_time = pixel_to_cycle(timing.htotal, true, mipi_cfg_param) + HLINE_TIME_PER_ADD;
	hfp = mipi->hline_time - mipi->dpi_hsize - mipi->hsa - mipi->hbp;

	ret = calculate_mipi_clk(mipi, timing);
	if (mipi->dsi_bit_clk > 0)
		hfp_time = hfp * 1000 * mipi_cfg_param.dsi_lane_byteclk_unit / mipi->dsi_bit_clk;

	// 单mipi 0.6us  双mipi 1.2us  0.6 * 1000 = 600
	while (hfp_time < MIN_HFP_TIME * mipi_cfg_param.lane_nums / LANE_NUMS_UNIT) {
		mipi->hline_time += HLINE_TIME_PER_ADD;
		hfp = (int)(mipi->hline_time - mipi->dpi_hsize - mipi->hsa - mipi->hbp);
		ret = calculate_mipi_clk(mipi, timing);
		if (mipi->dsi_bit_clk > 0)
			hfp_time = hfp * 1000 * mipi_cfg_param.dsi_lane_byteclk_unit / mipi->dsi_bit_clk;
	}

	dpu_pr_info("output param:");
	dpu_pr_info("dsi_bit_clk=%u, hline_time=%u, hsa=%u, vbp=%u, hfp=%d"
		   " vsa=%u, vfp=%u, vactive_line=%u, dpi_hsize=%u, hbp=%u",
		   mipi->dsi_bit_clk, mipi->hline_time, mipi->hsa,
		   mipi->vbp, hfp, mipi->vsa, mipi->vfp, mipi->vactive_line,
		   mipi->dpi_hsize, mipi->hbp);

	return ret;
}

int mipi_dsi_param_calculate(struct dkmd_connector_info *pinfo, struct dpu_connector *connector, struct panel_timing_cfg timing)
{
	struct mipi_config_param mipi_cfg_param;
	struct mipi_panel_info *mipi = NULL;

	if (!connector || !pinfo) {
		dpu_pr_err("connector or pinfo is nullptr!");
		return -EINVAL;
	}
 
	mipi = &(connector->post_info[connector->active_idx]->mipi);
	if (mipi->phy_mode == DPHY_MODE) {
		mipi_cfg_param.packet_header_size = DPHY_PACKET_HEADER_SIZE;
		mipi_cfg_param.byte_per_cycle = DPHY_BYTE_PER_CYCLE;
		mipi_cfg_param.dsi_lane_byteclk_unit = DPHY_DSI_LANEBYTECLK_UNIT;
	} else {
		mipi_cfg_param.packet_header_size = CPHY_PACKET_HEADER_SIZE;
		mipi_cfg_param.byte_per_cycle = CPHY_BYTE_PER_CYCLE;
		mipi_cfg_param.dsi_lane_byteclk_unit = CPHY_DSI_LANEBYTECLK_UNIT;
	}

	if (pinfo->bpp == LCD_RGB888)
		mipi_cfg_param.bpp = 24;
	else if (pinfo->bpp == LCD_RGB565)
		mipi_cfg_param.bpp = 16;
	else
		mipi_cfg_param.bpp = 24;

	if (connector->bind_connector)
		mipi_cfg_param.lane_nums = LANE_NUMS_UNIT * 2;
	else
		mipi_cfg_param.lane_nums = LANE_NUMS_UNIT;

	return set_mipi_dsi_timing_param(mipi, timing, mipi_cfg_param);
}