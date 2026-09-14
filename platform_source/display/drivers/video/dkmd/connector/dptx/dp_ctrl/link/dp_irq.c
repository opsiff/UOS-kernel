/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2021. All rights reserved.
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

#include "controller/dp_core_interface.h"
#include "controller/dp_avgen_base.h"
#include "dp_link_training.h"
#include "dp_irq.h"
#include "dp_edid.h"
#include "dp_aux.h"
#include "dp_drv.h"
#include "dp_mst_topology.h"
#include "dp_dsc_algorithm.h"
#include "drm_dp_helper_additions.h"
#include "dpu_dp_dbg.h"
#include "hdcp_common.h"
#include "psr_config_base.h"
#include "dp_ctrl_config.h"
#include "dp_ctrl_dev.h"
#include "dp_maintenance.h"
#include "hidptx_reg.h"
#include "dvfs.h"
#include <securec.h>
#include "utils/comm_utils.h"
#include "edid_timing_select.h"
#include "dsc/dsc_config_base.h"
#include "hidptx_dp_avgen.h"

#define SAFE_MODE_TIMING_HACTIVE 640
#define SAFE_MODE_TIMING_PIXEL_CLOCK 2517 /* The pixel clock of 640 * 480 = 25175. The saving pixel clock need 1/10. */
#define DPTX_CHECK_TIME_PERIOD 2000
#define EDID_BLOCK_INDEX 126
#define EDID_BLOCK_MAX 10
#define EDID_BLOCK_DEFAULT 0
#define GET_DEVICE_CAPS_TIMES 6

#define REG_VALUE_RATE_RBR 0x1FA4
#define REG_VALUE_RATE_HBR 0x34BC
#define REG_VALUE_RATE_HBR2 0x6978
#define REG_VALUE_RATE_HBR3 0x9E34

#define EDID_MAX_LEN 10
#define SCREEN_MAX_NUM 5
#define GET_DEVICE_CAPS_INTERVAL 100
#define EDID_READ_RETRY 2
#define EDID_READ_TIMEOUT 6000

#define DP_PLUGOUT_INTER_COUNT (3)

uint32_t g_max_line_cnt = 656; // = (maxfps(noVrr) / minfps(inVrr)) * vtotal(480 + 45) 
uint32_t g_enable_adpative_sync = 0;	
uint32_t g_min_interval = 12; // ms 	
uint32_t g_max_interval = 20; // ms 	
static uint32_t cnt_fresh = 0;
static uint32_t adaptive_interval = 16;
uint32_t g_out_adpative_sync = 0;

void test_adaptive_sync_val(uint32_t max_line_cnt, uint32_t enable_adpative_sync,
	uint32_t min_interval, uint32_t max_interval)
{
	g_max_line_cnt = max_line_cnt;
	g_enable_adpative_sync = enable_adpative_sync;
	g_min_interval = min_interval;
	g_max_interval = max_interval;
	adaptive_interval = g_min_interval;
	dpu_pr_info("[DP] test_adaptive_sync_val:%d,%d,%d,%d \n",
		max_line_cnt, enable_adpative_sync, min_interval, max_interval);
}
EXPORT_SYMBOL_GPL(test_adaptive_sync_val);

void output_adaptive_sync_en(uint32_t out_adpative_sync)
{
	g_out_adpative_sync = out_adpative_sync;
}
EXPORT_SYMBOL_GPL(output_adaptive_sync_en);

static enum hrtimer_restart test_adpative_sync_handler(struct hrtimer *timer)
{
	struct dp_ctrl *dptx = NULL;
	uint32_t phyifctrl;
	dpu_check_and_return((timer == NULL), HRTIMER_NORESTART, err, "[DP] timer is NULL!\n");
	dptx = container_of(timer, struct dp_ctrl, dptx_hrtimer);
	dpu_check_and_return((dptx == NULL), HRTIMER_NORESTART, err, "[DP] dptx is NULL!\n");

	if ((!dptx->dptx_enable) || (!dptx->video_transfer_enable) || (dptx->power_saving_mode))
		return HRTIMER_NORESTART;

	phyifctrl = dptx_readl(dptx, 0x90C);
	phyifctrl |= BIT(1);
	dptx_writel(dptx, 0x90C, phyifctrl);
	cnt_fresh++;
	if (cnt_fresh % 500 == 0) {
		adaptive_interval++;
		if (adaptive_interval >= g_max_interval)
			adaptive_interval = g_min_interval;
		dpu_pr_info("[DP] Vsync interval = %u ms\n", adaptive_interval);
	}
	hrtimer_start(&dptx->dptx_hrtimer, ktime_set(0, adaptive_interval * 1000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static void config_adaptive_sync(struct dp_ctrl *dptx, uint32_t max_line_cnt)
{	
	int retval;	
	uint32_t phyifctrl;	
	uint8_t byte = 0;

	dpu_check_and_no_retval((dptx == NULL), err, "[DP] dptx is NULL!\n");

	retval = dptx_read_dpcd(dptx, ADAPTIVE_SYNC_CAPABLITY, &byte);
	dpu_pr_info("byte = 0x%x \n", byte);
	if (byte & ADAPTIVE_SYNC_SDP_SUPPORTED) { // it's ok if this is false 
		dpu_pr_info("ADAPTIVE_SYNC_SDP_SUPPORTED is enabled, retval=%d", retval);
		retval = dptx_read_dpcd(dptx, DP_DOWN_STREAM_PORT_COUNT, &byte);
		if (byte & DP_MSA_TIMING_PAR_IGNORED) {
			dpu_pr_info("DP_MSA_TIMING_PAR_IGNORED is enabled, retval=%d", retval);
			phyifctrl = dptx_readl(dptx, DPTX_EDP_CTRL0);
			phyifctrl |= DPTX_CFG_ADPTIVE_SYNC_SDP_TX_ENABLE;
			dptx_writel(dptx, DPTX_EDP_CTRL0, phyifctrl);
		} else {
			dpu_pr_info("DP_MSA_TIMING_PAR_IGNORED is disabled, retval=%d", retval);
		}
	} else {
		dpu_pr_info("ADAPTIVE_SYNC_SDP_SUPPORTED is disabled, retval=%d", retval);
	}

	retval = dptx_read_dpcd(dptx, DP_DOWNSPREAD_CTRL, &byte);
	byte |= DP_MSA_TIMING_PAR_IGNORE_EN;
	dpu_pr_info("config_adaptive_sync byte = 0x%x, retval=%d\n", byte, retval);
	retval = dptx_write_dpcd(dptx, DP_DOWNSPREAD_CTRL, byte);
	if (retval) {
		dpu_pr_err("[DP] write DPCD error\n");
		return;
	}

	phyifctrl = dptx_readl(dptx, DPTX_LP_VBK_CTRL);
	phyifctrl &= ~GENMASK(21, 2);
	phyifctrl |= max_line_cnt << 2;
	phyifctrl |= DPTX_CFG_DYNAMIC_FRM_ENABLE;
	dpu_pr_info("config_adaptive_sync phyifctrl : 0x%x max_line_cnt : 0x%x \n",
		phyifctrl, max_line_cnt);
	dptx_writel(dptx, DPTX_LP_VBK_CTRL, phyifctrl);
}

static void dptx_enable_crc(struct dp_ctrl *dptx)
{
	uint8_t reg = 0;
	int retval = 0;
	uint32_t val = 0;

	val = dptx_readl(dptx, dptx_video_ctrl_stream(0));
	val |= DPTX_CFG_STREAM_DSC_CRC_ENABLE;
	dptx_writel(dptx,  dptx_video_ctrl_stream(0), val);
	dpu_pr_info("[DP] enable dptx crc value is: 0x%x", val);

	if (g_dp_debug_crc_enable || !dptx_is_usb_dp_panel(dptx->port_id)) {
		retval = dptx_read_dpcd(dptx, DP_TEST_SINK, &reg);
		if (retval != 0) {
			dpu_pr_err("[DP] failed to dptx_read_dpcd DP_TEST_SINK, retval=%d", retval);
			return;
		}
		reg |= DP_TEST_SINK_START;
		retval = dptx_write_dpcd(dptx, DP_TEST_SINK, reg);
		if (retval != 0) {
			dpu_pr_err("[DP] failed to dptx_write_dpcd DP_TEST_SINK, retval=%d", retval);
			return;
		}
		dpu_pr_info("[DP] enable dprx crc value is: 0x%x", reg);
	}
}

void dptx_rgb_crc_check(struct dp_ctrl *dptx)
{
	int retval;
	uint16_t crc_dptx_r = 0;
	uint16_t crc_dptx_g = 0;
	uint16_t crc_dptx_b = 0;
	uint16_t crc_dprx_r = 0;
	uint16_t crc_dprx_g = 0;
	uint16_t crc_dprx_b = 0;
	uint8_t crc_dpct[6] = {0};

	dptx->crc_count++;
	if (dptx->crc_count >= MAX_PRINTF_COUNT) {
		dptx->crc_count = 0;
		// read DPTX reg PSR RGB CRC
		(void)dp_ctrl_get_crc_value(dptx->connector, NULL);
		crc_dptx_r = (uint16_t)(dptx->crc_value.crc_data_r_g & DPTX_CFG_STREAM_CRC_ENGINE0_R_MASK);
		crc_dptx_g = (uint16_t)((dptx->crc_value.crc_data_r_g & DPTX_CFG_STREAM_CRC_ENGINE1_G_MASK) >> 16);
		crc_dptx_b = (uint16_t)(dptx->crc_value.crc_data_b & DPTX_CFG_STREAM_CRC_ENGINE2_B_MASK);
		
		retval = dptx_read_bytes_from_dpcd(dptx, DP_TEST_CRC_R_CR_LSB, crc_dpct, sizeof(crc_dpct));
		if (retval != 0) {
			dpu_pr_warn("[DP] DPTX read crc dpcd failed");
			return;
		}
		// read dpcd reg PSR RGB CRC
		crc_dprx_r = ((uint16_t)crc_dpct[1] << 8) | crc_dpct[0];
		crc_dprx_g = ((uint16_t)crc_dpct[3] << 8) | crc_dpct[2];
		crc_dprx_b = ((uint16_t)crc_dpct[5] << 8) | crc_dpct[4];

		if (dptx->crc_rgb.source_crc_r != crc_dptx_r ||
				dptx->crc_rgb.source_crc_g != crc_dptx_g || dptx->crc_rgb.source_crc_b != crc_dptx_b ||
				dptx->crc_rgb.sink_crc_r != crc_dprx_r || dptx->crc_rgb.sink_crc_g != crc_dprx_g ||
				dptx->crc_rgb.sink_crc_b != crc_dprx_b) {
			dptx->crc_rgb.source_crc_r = crc_dptx_r;
			dptx->crc_rgb.source_crc_g = crc_dptx_g;
			dptx->crc_rgb.source_crc_b = crc_dptx_b;
			dptx->crc_rgb.sink_crc_r = crc_dprx_r;
			dptx->crc_rgb.sink_crc_g = crc_dprx_g;
			dptx->crc_rgb.sink_crc_b = crc_dprx_b;
			dpu_pr_info("[DP] Source CRC R:0x%x G:0x%x B:0x%x, Sink CRC R:0x%x G:0x%x B:0x%x, port_id:%d",
				crc_dptx_r, crc_dptx_g, crc_dptx_b, crc_dprx_r, crc_dprx_g, crc_dprx_b, dptx->port_id);
		}
	}
}

void dptx_check_link_data_error(struct dp_ctrl *dptx)
{
	dpu_check_and_no_retval(!dptx, err, "[DP] dptx NULL Pointer\n");
	if (!dptx_is_usb_dp_panel(dptx->port_id))
		return;

	dptx_rgb_crc_check(dptx);
	return;
}

static int dptx_init_detect_work(struct dp_ctrl *dptx)
{
	dpu_pr_info("[DP] Init Detect work");
	if (!dptx->dptx_detect_inited) {
		/* hrtimer for detecting error count */
		hrtimer_init(&dptx->dptx_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		if (g_out_adpative_sync != 0) {
			dptx->dptx_hrtimer.function = test_adpative_sync_handler;
			hrtimer_start(&dptx->dptx_hrtimer, ktime_set(DPTX_CHECK_TIME_PERIOD / 1000,
				(DPTX_CHECK_TIME_PERIOD % 1000) * 1000000), HRTIMER_MODE_REL);
		}
		dptx->dptx_detect_inited = true;
	}

	return 0;
}

int dptx_cancel_detect_work(struct dp_ctrl *dptx)
{
	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	dpu_pr_info("[DP] Cancel Detect work");

	if (dptx->dptx_detect_inited) {
		hrtimer_cancel(&dptx->dptx_hrtimer);
		dptx->dptx_detect_inited = false;
	}

	return 0;
}

static int handle_test_link_training(struct dp_ctrl *dptx)
{
	int retval;
	uint8_t lanes = 0;
	uint8_t rate = 0;
	struct video_params *vparams = NULL;
	struct dtd *mdtd = NULL;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	retval = dptx_read_dpcd(dptx, DP_TEST_LINK_RATE, &rate);
	if (retval)
		return retval;

	rate = dptx_bw_to_phy_rate(rate);
	if (rate == DPTX_PHYIF_CTRL_RATE_INVALID)
		return -EINVAL;

	retval = dptx_read_dpcd(dptx, DP_TEST_LANE_COUNT, &lanes);
	if (retval)
		return retval;

	dpu_pr_info("[DP] Starting link training rate=%u, lanes=%u", rate, lanes);

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;

	if (dptx->dptx_video_ts_calculate)
		retval = dptx->dptx_video_ts_calculate(dptx, lanes, rate,
					 vparams->bpc, vparams->pix_enc,
					 mdtd->pixel_clock);
	if (retval)
		return retval;

	if (g_dp_debug_jump_linktraining_enable) {
		dpu_pr_warn("[DP] g_dp_debug_jump_linktraining_enable enable");
		return -ECONNREFUSED;
	}

	retval = dptx_link_retraining(dptx, rate, lanes);
	if (retval)
		dpu_pr_err("[DP] Link training failed %d", retval);
	else
		dpu_pr_info("[DP] Link training succeeded");

	return retval;
}

static int handle_test_get_video_mode(struct test_dtd tdtd, enum video_format_type *video_format, uint8_t *vmode)
{
	int i;

	*vmode = 0;
	for (i = 0; i < ARRAY_SIZE(test_timing); i++) {
		struct test_dtd tmp = test_timing[i];

		if (tdtd.h_total == tmp.h_total && tdtd.v_total == tmp.v_total && tdtd.h_start == tmp.h_start &&
			tdtd.v_start == tmp.v_start && tdtd.h_sync_width == tmp.h_sync_width
			&& tdtd.v_sync_width == tmp.v_sync_width &&
			tdtd.h_width == tmp.h_width && tdtd.v_width == tmp.v_width) {
			*video_format = tmp.video_format;
			*vmode = tmp.vmode;
			break;
		}
	}

	if ((*video_format == DMT) && (*vmode == 82) && (tdtd.refresh_rate == 120000)) {
		*vmode = 63;
		*video_format = CTA;
	}

	if (*vmode == 0) {
		dpu_pr_info("[DP] Unknown video mode");
		return -EINVAL;
	}

	return 0;
}

static int handle_test_read_video_timing(struct dp_ctrl *dptx, struct test_dtd *tdtd)
{
	int retval;
	uint8_t bytes[19] = {0};

	retval = dptx_read_bytes_from_dpcd(dptx, DP_TEST_H_TOTAL_MSB, bytes, sizeof(bytes));
	if (retval)
		return retval;

	tdtd->h_total = (bytes[0] << 8) | bytes[1];
	tdtd->v_total = (bytes[2] << 8) | bytes[3];
	tdtd->h_start = (bytes[4] << 8) | bytes[5];
	tdtd->v_start = (bytes[6] << 8) | bytes[7];
	tdtd->h_sync_width = ((bytes[8] & (~(1 << 7))) << 8) | bytes[9];
	tdtd->h_sync_pol = bytes[8]  >> 7;
	tdtd->v_sync_width = ((bytes[10] & (~(1 << 7))) << 8) | bytes[11];
	tdtd->v_sync_pol = bytes[10]  >> 7;
	tdtd->h_width = (bytes[12] << 8) | bytes[13];
	tdtd->v_width = (bytes[14] << 8) | bytes[15];
	tdtd->refresh_rate = bytes[18] * 1000;

	dpu_pr_info("[DP] h_total=%d", tdtd->h_total);
	dpu_pr_info("[DP] v_total=%d", tdtd->v_total);
	dpu_pr_info("[DP] h_start=%d", tdtd->h_start);
	dpu_pr_info("[DP] v_start=%d", tdtd->v_start);
	dpu_pr_info("[DP] h_sync_width=%d", tdtd->h_sync_width);
	dpu_pr_info("[DP] h_sync_pol=%d", tdtd->h_sync_pol);
	dpu_pr_info("[DP] v_sync_width=%d", tdtd->v_sync_width);
	dpu_pr_info("[DP] v_sync_pol=%d", tdtd->v_sync_pol);
	dpu_pr_info("[DP] h_width=%d", tdtd->h_width);
	dpu_pr_info("[DP] v_width=%d", tdtd->v_width);
	dpu_pr_info("[DP] refresh_rate=%d", tdtd->refresh_rate);

	return 0;
}

static int handle_test_link_video_timming(struct dp_ctrl *dptx, int stream)
{
	int retval;
	enum video_format_type video_format;
	uint8_t vmode;
	struct video_params *vparams = NULL;
	struct dtd mdtd;
	struct test_dtd tdtd;

	memset(&tdtd, 0, sizeof(tdtd));
	retval = handle_test_read_video_timing(dptx, &tdtd);
	if (retval)
		return retval;

	video_format = DMT;
	retval = handle_test_get_video_mode(tdtd, &video_format, &vmode);
	if (retval)
		return retval;

	if (!convert_code_to_dtd(&mdtd, vmode, tdtd.refresh_rate, video_format)) {
		dpu_pr_info("[DP] Invalid video mode value %u", vmode);
		retval = -EINVAL;
		return retval;
	}

	vparams = &dptx->vparams;
	vparams->mdtd = mdtd;
	vparams->refresh_rate = tdtd.refresh_rate;
	if (dptx->dptx_video_ts_calculate)
		retval = dptx->dptx_video_ts_calculate(dptx, dptx->link.lanes,
					 dptx->link.rate, vparams->bpc,
					 vparams->pix_enc, mdtd.pixel_clock);
	if (retval)
		return retval;

	/* MMCM */
	if (dptx->dptx_resolution_switch)
		dptx->dptx_resolution_switch(dptx, HOT_PLUG_TEST);

	if (dptx->dptx_video_timing_change)
		dptx->dptx_video_timing_change(dptx, 0);

	return 0;
}

static int handle_test_get_audio_channel_count(struct dp_ctrl *dptx, uint8_t test_audio_ch_count)
{
	struct audio_params *aparams = NULL;
	uint8_t  audio_ch_count;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	aparams = &dptx->aparams;
	switch (test_audio_ch_count) {
	case DP_TEST_AUDIO_CHANNEL1:
		dpu_pr_info("[DP] DP_TEST_AUDIO_CHANNEL1");
		audio_ch_count = 1;
		break;
	case DP_TEST_AUDIO_CHANNEL2:
		dpu_pr_info("[DP] DP_TEST_AUDIO_CHANNEL2");
		audio_ch_count = 2;
		break;
	case DP_TEST_AUDIO_CHANNEL3:
		dpu_pr_info("[DP] DP_TEST_AUDIO_CHANNEL3");
		audio_ch_count = 3;
		break;
	case DP_TEST_AUDIO_CHANNEL4:
		dpu_pr_info("[DP] DP_TEST_AUDIO_CHANNEL4");
		audio_ch_count = 4;
		break;
	case DP_TEST_AUDIO_CHANNEL5:
		dpu_pr_info("[DP] DP_TEST_AUDIO_CHANNEL5");
		audio_ch_count = 5;
		break;
	case DP_TEST_AUDIO_CHANNEL6:
		dpu_pr_info("[DP] DP_TEST_AUDIO_CHANNEL6");
		audio_ch_count = 6;
		break;
	case DP_TEST_AUDIO_CHANNEL7:
		dpu_pr_info("[DP] DP_TEST_AUDIO_CHANNEL7");
		audio_ch_count = 7;
		break;
	case DP_TEST_AUDIO_CHANNEL8:
		dpu_pr_info("[DP] DP_TEST_AUDIO_CHANNEL8");
		audio_ch_count = 8;
		break;
	default:
		dpu_pr_info("[DP] Invalid TEST_AUDIO_CHANNEL_COUNT");
		return -EINVAL;
	}
	dpu_pr_info("[DP] test_audio_ch_count=%u", audio_ch_count);
	aparams->num_channels = audio_ch_count;

	return 0;
}

static int handle_test_get_audio_sample_params(struct dp_ctrl *dptx, uint8_t test_audio_smaple_range)
{
	struct audio_params *aparams = NULL;
	uint8_t  orig_sample_freq, sample_freq;
	uint32_t audio_clock_freq;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	aparams = &dptx->aparams;
	switch (test_audio_smaple_range) {
	case DP_TEST_AUDIO_SAMPLING_RATE_32:
		dpu_pr_info("[DP] DP_TEST_AUDIO_SAMPLING_RATE_32");
		orig_sample_freq = 12;
		sample_freq = 3;
		audio_clock_freq = 320;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_44_1:
		dpu_pr_info("[DP] DP_TEST_AUDIO_SAMPLING_RATE_44_1");
		orig_sample_freq = 15;
		sample_freq = 0;
		audio_clock_freq = 441;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_48:
		dpu_pr_info("[DP] DP_TEST_AUDIO_SAMPLING_RATE_48");
		orig_sample_freq = 13;
		sample_freq = 2;
		audio_clock_freq = 480;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_88_2:
		dpu_pr_info("[DP] DP_TEST_AUDIO_SAMPLING_RATE_88_2");
		orig_sample_freq = 7;
		sample_freq = 8;
		audio_clock_freq = 882;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_96:
		dpu_pr_info("[DP] DP_TEST_AUDIO_SAMPLING_RATE_96");
		orig_sample_freq = 5;
		sample_freq = 10;
		audio_clock_freq = 960;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_176_4:
		dpu_pr_info("[DP] DP_TEST_AUDIO_SAMPLING_RATE_176_4");
		orig_sample_freq = 3;
		sample_freq = 12;
		audio_clock_freq = 1764;
		break;
	case DP_TEST_AUDIO_SAMPLING_RATE_192:
		dpu_pr_info("[DP] DP_TEST_AUDIO_SAMPLING_RATE_192");
		orig_sample_freq = 1;
		sample_freq = 14;
		audio_clock_freq = 1920;
		break;
	default:
		dpu_pr_info("[DP] Invalid TEST_AUDIO_SAMPLING_RATE");
		return -EINVAL;
	}
	dpu_pr_info("[DP] sample_freq=%u", sample_freq);
	dpu_pr_info("[DP] orig_sample_freq=%d", orig_sample_freq);
	dpu_pr_info("[DP] audio_clock_freq=%d", audio_clock_freq);

	aparams->iec_samp_freq = sample_freq;
	aparams->iec_orig_samp_freq = orig_sample_freq;

	return 0;
}

static int handle_test_link_audio_pattern(struct dp_ctrl *dptx)
{
	int retval;
	uint8_t test_audio_mode = 0;
	uint8_t test_audio_smaple_range;
	uint8_t test_audio_ch_count;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	retval = dptx_read_dpcd(dptx, DP_TEST_AUDIO_MODE, &test_audio_mode);
	if (retval)
		return retval;

	dpu_pr_info("[DP] test_audio_mode= %u", test_audio_mode);

	test_audio_smaple_range = test_audio_mode & DP_TEST_AUDIO_SAMPLING_RATE_MASK;
	test_audio_ch_count = (test_audio_mode & DP_TEST_AUDIO_CH_COUNT_MASK)
		>> DP_TEST_AUDIO_CH_COUNT_SHIFT;

	retval = handle_test_get_audio_channel_count(dptx, test_audio_ch_count);
	if (retval)
		return retval;

	retval = handle_test_get_audio_sample_params(dptx, test_audio_smaple_range);
	if (retval)
		return retval;

	if (dptx->dptx_audio_num_ch_change)
		dptx->dptx_audio_num_ch_change(dptx);
	if (dptx->dptx_audio_infoframe_sdp_send)
		dptx->dptx_audio_infoframe_sdp_send(dptx);

	return retval;
}

static int calc_dynamic_range_map(uint8_t dynamic_range, uint8_t *dynamic_range_map)
{
	switch (dynamic_range) {
	case DP_TEST_DYNAMIC_RANGE_VESA:
		dpu_pr_info("[DP] DP_TEST_DYNAMIC_RANGE_VESA");
		*dynamic_range_map = VESA;
		break;
	case DP_TEST_DYNAMIC_RANGE_CEA:
		dpu_pr_info("[DP] DP_TEST_DYNAMIC_RANGE_CEA");
		*dynamic_range_map = CEA;
		break;
	default:
		dpu_pr_info("[DP] Invalid TEST_BIT_DEPTH");
		return -EINVAL;
	}

	return 0;
}

static int calc_ycbcr_coeff_map(uint8_t ycbcr_coeff, uint8_t *ycbcr_coeff_map)
{
	switch (ycbcr_coeff) {
	case DP_TEST_YCBCR_COEFF_ITU601:
		dpu_pr_info("[DP] DP_TEST_YCBCR_COEFF_ITU601");
		*ycbcr_coeff_map = ITU601;
		break;
	case DP_TEST_YCBCR_COEFF_ITU709:
		dpu_pr_info("[DP] DP_TEST_YCBCR_COEFF_ITU709:");
		*ycbcr_coeff_map = ITU709;
		break;
	default:
		dpu_pr_info("[DP] Invalid TEST_BIT_DEPTH");
		return -EINVAL;
	}

	return 0;
}

static int calc_color_format_map(uint8_t color_format, uint8_t *color_format_map)
{
	switch (color_format) {
	case DP_TEST_COLOR_FORMAT_RGB:
		dpu_pr_info("[DP] DP_TEST_COLOR_FORMAT_RGB");
		*color_format_map = RGB;
		break;
	case DP_TEST_COLOR_FORMAT_YCBCR422:
		dpu_pr_info("[DP] DP_TEST_COLOR_FORMAT_YCBCR422");
		*color_format_map = YCBCR422;
		break;
	case DP_TEST_COLOR_FORMAT_YCBCR444:
		dpu_pr_info("[DP] DP_TEST_COLOR_FORMAT_YCBCR444");
		*color_format_map = YCBCR444;
		break;
	default:
		dpu_pr_info("[DP] Invalid  DP_TEST_COLOR_FORMAT");
		return -EINVAL;
	}

	return 0;
}

static int calc_bpc_map(uint8_t bpc, uint8_t *bpc_map)
{
	switch (bpc) {
	case DP_TEST_BIT_DEPTH_6:
		*bpc_map = COLOR_DEPTH_6;
		dpu_pr_info("[DP] TEST_BIT_DEPTH_6");
		break;
	case DP_TEST_BIT_DEPTH_8:
		*bpc_map = COLOR_DEPTH_8;
		dpu_pr_info("[DP] TEST_BIT_DEPTH_8");
		break;
	case DP_TEST_BIT_DEPTH_10:
		*bpc_map = COLOR_DEPTH_10;
		dpu_pr_info("[DP] TEST_BIT_DEPTH_10");
		break;
	case DP_TEST_BIT_DEPTH_12:
		*bpc_map = COLOR_DEPTH_12;
		dpu_pr_info("[DP] TEST_BIT_DEPTH_12");
		break;
	case DP_TEST_BIT_DEPTH_16:
		*bpc_map = COLOR_DEPTH_16;
		dpu_pr_info("[DP] TEST_BIT_DEPTH_16");
		break;
	default:
		dpu_pr_info("[DP] Invalid TEST_BIT_DEPTH");
		return -EINVAL;
	}

	return 0;
}

static int handle_test_link_video_pattern(struct dp_ctrl *dptx, int stream)
{
	int retval = 0;
	uint8_t misc = 0, pattern = 0;
	uint8_t bpc_map = 0, dynamic_range_map = 0;
	uint8_t color_format_map, ycbcr_coeff_map = 0;
	struct video_params *vparams = &dptx->vparams;

	retval = dptx_read_dpcd(dptx, DP_TEST_PATTERN, &pattern);
	if (retval)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_TEST_MISC0, &misc);
	if (retval)
		return retval;

	retval = calc_dynamic_range_map(misc & DP_TEST_DYNAMIC_RANGE_MASK, &dynamic_range_map);
	if (retval)
		return retval;

	retval = calc_ycbcr_coeff_map((misc & DP_TEST_YCBCR_COEFF_MASK) \
		>> DP_TEST_YCBCR_COEFF_SHIFT, &ycbcr_coeff_map);
	if (retval)
		return retval;

	retval = calc_color_format_map(misc & DP_TEST_COLOR_FORMAT_MASK, &color_format_map);
	if (retval)
		return retval;

	retval = calc_bpc_map(misc & DP_TEST_BIT_DEPTH_MASK, &bpc_map);
	if (retval)
		return retval;

	vparams->dynamic_range = dynamic_range_map;
	dpu_pr_info("[DP] Change video dynamic range to %u", dynamic_range_map);

	vparams->colorimetry = ycbcr_coeff_map;
	dpu_pr_info("[DP] Change video colorimetry to %u", ycbcr_coeff_map);

	if (dptx->dptx_video_ts_calculate)
		retval = dptx->dptx_video_ts_calculate(
			dptx, dptx->link.lanes,
			dptx->link.rate,
			bpc_map,
			color_format_map,
			vparams->mdtd.pixel_clock);
	if (retval)
		return retval;

	vparams->pix_enc = color_format_map;
	dpu_pr_info("[DP] Change pixel encoding to %d", color_format_map);
	vparams->bpc = bpc_map;

	if (dptx->dptx_video_bpc_change)
		dptx->dptx_video_bpc_change(dptx, stream);
	dpu_pr_info("[DP] Change bits per component to %d", bpc_map);

	if (dptx->dptx_video_ts_change)
		dptx->dptx_video_ts_change(dptx, stream);

	switch (pattern) {
	case DP_TEST_PATTERN_NONE:
		dpu_pr_info("[DP] TEST_PATTERN_NONE %d", pattern);
		break;
	case DP_TEST_PATTERN_COLOR_RAMPS:
		dpu_pr_info("[DP] TEST_PATTERN_COLOR_RAMPS %d", pattern);
		vparams->pattern_mode = RAMP;
		dpu_pr_info("[DP] Change video pattern to RAMP");
		break;
	case DP_TEST_PATTERN_BW_VERITCAL_LINES:
		dpu_pr_info("[DP] TEST_PATTERN_BW_VERTICAL_LINES %d", pattern);
		break;
	case DP_TEST_PATTERN_COLOR_SQUARE:
		dpu_pr_info("[DP] TEST_PATTERN_COLOR_SQUARE %d", pattern);
		vparams->pattern_mode = COLRAMP;
		dpu_pr_info("[DP] Change video pattern to COLRAMP");
		break;
	default:
		dpu_pr_info("[DP] Invalid TEST_PATTERN %d", pattern);
		return -EINVAL;
	}

	retval = handle_test_link_video_timming(dptx, stream);
	if (retval)
		return retval;

	return 0;
}

static int handle_automated_test_request(struct dp_ctrl *dptx)
{
	int retval;
	uint8_t test_request = 0;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	retval = dptx_read_dpcd(dptx, DP_TEST_REQUEST, &test_request);
	dpu_pr_info("[DP] dpcd 0x218: %llx", test_request);
	if (retval)
		return retval;

	if (test_request & DP_TEST_LINK_TRAINING) {
		dpu_pr_info("[DP] DP_TEST_LINK_TRAINING");

		retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_ACK);
		if (retval)
			return retval;

		retval = handle_test_link_training(dptx);
		if (retval)
			return retval;
	}

	if (test_request & DP_TEST_LINK_VIDEO_PATTERN) {
		dpu_pr_info("[DP] DP_TEST_LINK_VIDEO_PATTERN");

		retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_ACK);
		if (retval)
			return retval;

		dp_send_cable_notification(dptx, HOT_PLUG_TEST_OUT);

		retval = handle_test_link_video_pattern(dptx, 0);
		if (retval)
			return retval;
	}

	if (test_request & DP_TEST_LINK_AUDIO_PATTERN) {
		dpu_pr_info("[DP] DP_TEST_LINK_AUDIO_PATTERN");

		retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_ACK);
		if (retval)
			return retval;

		retval = handle_test_link_audio_pattern(dptx);
		if (retval)
			return retval;
	}

	if (test_request & DP_TEST_LINK_EDID_READ) {
		/* Invalid, this should happen on HOTPLUG */
		dpu_pr_info("[DP] DP_TEST_LINK_EDID_READ");
		return -ENOTSUPP;
	}

	if (test_request & DP_TEST_LINK_PHY_TEST_PATTERN) {
		dpu_pr_info("[DP] DP_TEST_LINK_PHY_TEST_PATTERN");
		if (dptx->dptx_triger_media_transfer)
			dptx->dptx_triger_media_transfer(dptx, false);

		retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_ACK);
		if (retval)
			return retval;

		if (dptx->dptx_phy_set_ssc)
			dptx->dptx_phy_set_ssc(dptx, true);

		(void)dptx_link_adjust_drive_settings(dptx, NULL); // Test only

		if (dptx->handle_test_link_phy_pattern)
			retval = dptx->handle_test_link_phy_pattern(dptx);
		if (retval)
			return retval;
	}

	return 0;
}

int handle_sink_request(struct dp_ctrl *dptx)
{
	int retval = 0;
	uint8_t vector = 0;
	uint8_t bytes[1] = {0};

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	dptx->is_need_reset_hardware = false;
	retval = dptx_link_check_status(dptx);
	if (retval != 0)
		return retval;

	retval = dptx_check_sink_error_status(dptx);
	if (retval != 0)
		return retval;

	if (dptx->is_need_reset_hardware) {
		dpu_pr_info("[DP] Reset hardware after abnormal irq_hpd");
		dptx_reset_hardware_notify(dptx);
		return 0;
	}

	retval = dptx_irq_hpd_link_recovery(dptx);
	if (retval != 0)
		return retval;

	retval = dptx_read_bytes_from_dpcd(dptx, DP_DEVICE_SERVICE_IRQ_VECTOR_ESI0, bytes, sizeof(bytes));
	if (retval != 0)
		return retval;

	retval = dptx_read_dpcd(dptx, DP_DEVICE_SERVICE_IRQ_VECTOR, &vector);
	if (retval != 0)
		return retval;

	dpu_pr_info("[DP] IRQ_VECTOR: 0x%02x", vector);
	dp_imonitor_set_param(DP_PARAM_IRQ_VECTOR, &vector);

	/* handle sink interrupts */
	if (vector == 0)
		return 0;

	if ((vector | bytes[0]) & DP_REMOTE_CONTROL_COMMAND_PENDING)
		dpu_pr_warn("[DP] DP_REMOTE_CONTROL_COMMAND_PENDING: Not yet implemented");

	if ((vector | bytes[0]) & DP_AUTOMATED_TEST_REQUEST) {
		dpu_pr_info("[DP] DP_AUTOMATED_TEST_REQUEST");
		retval = handle_automated_test_request(dptx);
		if (retval != 0) {
			dpu_pr_err("[DP] Automated test request failed");
			if (retval == -ENOTSUPP) {
				retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_NAK);
				if (retval != 0)
					return retval;
			}
		}
	}

	if ((vector | bytes[0]) & DP_CP_IRQ) {
		dpu_pr_warn("[DP] DP_CP_IRQ");
		switch_notification_event_unchange_state(&dptx->sdev, HOT_PLUG_HDCP_CP_IRQ);
	}

	if ((vector | bytes[0]) & DP_MCCS_IRQ) {
		dpu_pr_warn("[DP] DP_MCCS_IRQ: Not yet implemented");
		retval = -ENOTSUPP;
	}

	if ((vector | bytes[0]) & DP_UP_REQ_MSG_RDY) {
		dpu_pr_warn("[DP] DP_UP_REQ_MSG_RDY");
		retval = dptx_sideband_get_up_req(dptx);
		if (retval) {
			dpu_pr_err("[DP]: Error reading UP REQ %d", retval);
			return retval;
		}
	}

	if ((vector | bytes[0]) & DP_SINK_SPECIFIC_IRQ) {
		dpu_pr_warn("[DP] DP_SINK_SPECIFIC_IRQ: Not yet implemented");
		retval = -ENOTSUPP;
	}

	return retval;
}

int handle_hotunplug(struct dp_ctrl *dptx)
{
	struct dkmd_connector_info *pinfo = NULL;
	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");
	pinfo = dptx->connector->conn_info;
	dpu_check_and_return(!pinfo, -EINVAL, err, "[DP] pinfo is NULL!");
	dpu_check_and_return(!dptx->dptx_enable, -EINVAL, err, "[DP] dptx has already off!");
	dptx->is_timing_changed = false;

	dpu_pr_info("[DP] +");

	/* Disable DPTX video */
	if (dptx->dptx_disable_default_video_stream && !dptx->power_saving_mode)
		dptx->dptx_disable_default_video_stream(dptx, 0);

	if (dptx->dptx_triger_media_transfer && !dptx->power_saving_mode)
		dptx->dptx_triger_media_transfer(dptx, false);
	mdelay(20);

	dptx->video_transfer_enable = false;
	dptx->max_edid_timing_hactive = 0;
	dptx->dummy_dtds_present = false;

	dptx_cancel_detect_work(dptx);

	dptx_video_params_reset(&dptx->vparams, dptx->bit_depth);
	dptx_audio_params_reset(&dptx->aparams);
 
	/* Notify hwc */
	dp_send_cable_notification(dptx, (dptx->dptx_vr) ? HOT_PLUG_OUT_VR : HOT_PLUG_OUT);

	dptx_wait_for_last_frame_finished(pinfo, SLEEP_HANDLE);

	if (dptx->dptx_link_close_stream && !dptx->power_saving_mode)
		dptx->dptx_link_close_stream(dptx);

	dpu_pr_info("[DP] close stream");

	release_edid_info(dptx);
	atomic_set(&dptx->sink_request, 0);
	atomic_set(&dptx->aux.event, 0);
	dptx->link.trained = false;
	dptx->dsc = false;
	dptx->dptx_link_params.compress_params.fec = false;
	dptx->is_pps_send_manual = false;
	dptx->psr_params.psr_version = INVALID_PSR_CAP;
	dptx->is_new_frame_update = false;
	dptx->is_need_fix_rate = false;

	dptx_pixel_pll_deinit(dptx);

	// for cdc_ace, after dp disconnect need reset 983.
	if (dptx->dp_info && dptx->dp_info->notify_ser_dp_unplug)
		dptx->dp_info->notify_ser_dp_unplug(dptx->port_id);

	/* for test_dynamic */
	if (g_enable_adpative_sync) {
		uint32_t phyifctrl = 0;
		phyifctrl = dptx_readl(dptx, DPTX_LP_VBK_CTRL);
		phyifctrl &= ~DPTX_CFG_DYNAMIC_FRM_ENABLE;
		dptx_writel(dptx, DPTX_LP_VBK_CTRL, phyifctrl);
	}

	dp_imonitor_set_param(DP_PARAM_TIME_STOP, NULL);
	dpu_dvfs_set_inter_vote_index_count(DP_PLUGOUT_INTER_COUNT);
	dpu_pr_info("[DP] -");

	return 0;
}

static bool dptx_edid_one_block_check(struct dp_ctrl *dptx, uint8_t* edid)
{
	int edid_index = 0;
	uint32_t checksum = 0;
	for (edid_index = 0; edid_index < EDID_LENGTH; edid_index++)
			checksum += edid[edid_index];
	if (checksum & 0xFF) {
		dpu_pr_warn("[DP] EDID checksum failed - data is corrupt. checksum is %u", checksum);
		dp_maintenance_add_error_log(dptx, "EDIDchecksumfailed");
		return false;
	}
	return true;
}

static int dptx_aux_read_edid_by_line(struct dp_ctrl *dptx, unsigned int block, uint32_t len)
{
	int retval;
	uint32_t i;
	uint8_t  curlen;
	uint8_t  finallen;
	uint8_t segment = (uint8_t)(block >> 1);
	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	for (i = 0; i < len; ) {
		curlen = min_t(uint32_t, len - i, 16);
		finallen = i + block * EDID_BLOCK_LENGTH;
		dpu_pr_info("[DP] start read finallen: %u, i:%u", finallen, i);

		if (segment != 0) {
			retval = dptx_write_bytes_to_i2c(dptx, 0x30, &segment, 1);
			if (retval) {
				dpu_pr_err("[DP] failed to dptx_write_bytes_to_i2c 1!");
				return retval;
			}
		}

		retval = dptx_write_bytes_to_i2c(dptx, 0x50, &finallen, 1);
		if (retval) {
			dpu_pr_warn("[DP] failed to dptx_write_bytes_to_i2c 2!");
			if (dptx_i2c_address_only(dptx, 0x50) == -ETIMEDOUT)
				retval = -ETIMEDOUT;
			return retval;
		}

		retval = dptx_read_bytes_from_i2c(dptx, 0x50,
			&dptx->edid[i + block * EDID_BLOCK_LENGTH], curlen);
		if (retval) {
			dpu_pr_warn("[DP] failed to dptx_read_bytes_from_i2c 3!");
			if (dptx_i2c_address_only(dptx, 0x50) == -ETIMEDOUT)
				retval = -ETIMEDOUT;
			return retval;
		}
		mdelay(1);
		i += curlen;
	}

	return 0;
}

static int dptx_read_edid_block(struct dp_ctrl *dptx, unsigned int block)
{
	int retval = 0;
	int retry = 0;
	uint64_t delta_time = 0;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	dpu_pr_info("[DP] block=%d", block);
	/* Skip if no E-DDC */
	while (retry <= EDID_READ_RETRY) {
		dptx->edid_read_current_time = ktime_get();
		delta_time = (uint64_t)(ktime_to_ms(dptx->edid_read_current_time) - ktime_to_ms(dptx->edid_read_start_time));
		if (delta_time >= EDID_READ_TIMEOUT) {
			dpu_pr_warn("[DP] Read EDID timeout, take time is %llu", delta_time);
			return -ETIMEDOUT;
		}

		retval = dptx_aux_read_edid_by_line(dptx, block, EDID_BLOCK_LENGTH);
		/* detect link timeout */
		if (retval == -ETIMEDOUT || dptx->is_link_timeout) {
			dpu_pr_info("[DP] DP link is timeout!");
			return -ETIMEDOUT;
		}

		if (retval || !dptx_edid_one_block_check(dptx, &dptx->edid[block * EDID_BLOCK_LENGTH])) {
			dpu_pr_warn("[DP] failed to dptx_read_bytes_from_i2c 2 or check sum failed");
			dp_maintenance_add_error_log(dptx, "i2c_or_sum_failed");
			retry++;
			mdelay(3);
			continue;
		}
		break;
	}

	dp_imonitor_set_param(DP_PARAM_EDID + block, &(dptx->edid[block * DP_DSM_EDID_BLOCK_SIZE]));
	if (dptx_i2c_address_only(dptx, 0x50) == -ETIMEDOUT)
		retval = -ETIMEDOUT;

	return retval;
}

static bool dptx_check_edid_header(struct dp_ctrl *dptx)
{
	int i;
	uint8_t *edid_t = NULL;

	dpu_check_and_return(!dptx, false, err, "[DP] dptx is NULL!");
	dpu_check_and_return(!dptx->edid, false, err, "[DP] dptx->edid is NULL!");

	edid_t = dptx->edid;
	for (i = 0; i < EDID_HEADER_END + 1; i++) {
		if (edid_t[i] != edid_v1_header[i]) {
			dpu_pr_info("[DP] Invalide edid header[%d] %#x != %#x", i, edid_t[i], edid_v1_header[i]);
			return false;
		}
	}

	return true;
}

static int dptx_get_first_edid_block(struct dp_ctrl *dptx, unsigned int *ext_block_nums)
{
	int retval = 0;
	unsigned int ext_blocks = 0;
	int edid_try_count = 0;
	struct dp_private *dp_priv = NULL;

	dpu_check_and_return(!dptx->connector, -EINVAL, err, "[DP] dptx->connector is NULL!\n");
	dp_priv = to_dp_private(dptx->connector->conn_info);
	dpu_check_and_return(!dp_priv, -EINVAL, err, "[DP] dp_priv is NULL!\n");

	/* delay 20ms before read edid, adjust some monitor */
	dpu_pr_info("[DP] delay 20 ms before read edid");
	mdelay(20);

edid_retry:
	memset_s(dptx->edid, DPTX_DEFAULT_EDID_BUFLEN, 0, DPTX_DEFAULT_EDID_BUFLEN);
	retval = dptx_read_edid_block(dptx, 0);
	/* detect link timeout */
	if (retval == -ETIMEDOUT || dptx->is_link_timeout) {
		dpu_pr_info("[DP] DP link is timeout!");
		return -ETIMEDOUT;
	}
	/* will try to read edid block again when ready edid block failed */
	if (retval) {
		if ((uint32_t)edid_try_count <= dptx->edid_try_count) {
			dpu_pr_info("[DP] Read edid block failed, try %d times", edid_try_count);
			mdelay(dptx->edid_try_delay);
			edid_try_count += 1;
			goto edid_retry;
		} else {
			dpu_pr_err("[DP] failed to dptx_read_edid_block!");
			return -EINVAL;
		}
	}

	if (dptx->edid[126] > 10)
		/* Workaround for QD equipment */
		/* investigate corruptions of EDID blocks */
		ext_blocks = 2;
	else
		ext_blocks = dptx->edid[126];

	if ((ext_blocks > MAX_EXT_BLOCKS) || !dptx_check_edid_header(dptx)) {
		edid_try_count += 1;
		if (edid_try_count <= (int32_t)(dptx->edid_try_count)) {
			mdelay(dptx->edid_try_delay);
			dpu_pr_info("[DP] Read edid data is not correct, try %d times", edid_try_count);
			goto edid_retry;
		} else {
			if (ext_blocks > MAX_EXT_BLOCKS)
				ext_blocks = MAX_EXT_BLOCKS;
		}
	}
	*ext_block_nums = ext_blocks;

	return 0;
}

static int dptx_read_edid(struct dp_ctrl *dptx)
{
	int i;
	int retval = 0;
	unsigned int ext_blocks = 0;
	uint8_t *first_edid_block = NULL;
	unsigned int edid_buf_size = 0;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");
	dpu_check_and_return(!dptx->edid, -EINVAL, err, "[DP] dptx->edid is NULL!");

	dpu_pr_info("[DP] +");

	dptx->edid_read_start_time = ktime_get();
	retval = dptx_get_first_edid_block(dptx, &ext_blocks);
	if (retval)
		return retval;

	first_edid_block = kmalloc(EDID_BLOCK_LENGTH, GFP_KERNEL);
	if (first_edid_block == NULL) {
		dpu_pr_err("[DP] Allocate buffer error");
		return -EINVAL;
	}
	memset_s(first_edid_block, EDID_BLOCK_LENGTH, 0, EDID_BLOCK_LENGTH);
	if (memcpy_s(first_edid_block, EDID_BLOCK_LENGTH, dptx->edid, EDID_BLOCK_LENGTH) != EOK) {
		dpu_pr_err("[DP] memcpy edid buffer error!");
		retval = -EINVAL;
		goto fail;
	}
	kfree(dptx->edid);
	dptx->edid = NULL;

	dptx->edid = kzalloc(EDID_BLOCK_LENGTH * ext_blocks + EDID_BLOCK_LENGTH, GFP_KERNEL);
	if (dptx->edid == NULL) {
		dpu_pr_err("[DP] Allocate edid buffer error!");
		retval = -EINVAL;
		goto fail;
	}

	if (memcpy_s(dptx->edid, EDID_BLOCK_LENGTH, first_edid_block, EDID_BLOCK_LENGTH) != EOK) {
		dpu_pr_err("[DP] memcpy edid buffer error!");
		retval = -EINVAL;
		goto fail;
	}
	retval = EDID_BLOCK_LENGTH;

	for (i = 1; i <= ext_blocks; i++) {
		retval = dptx_read_edid_block(dptx, i);
		if (retval != 0)
			goto fail;
	}

	edid_buf_size = EDID_BLOCK_LENGTH * ext_blocks + EDID_BLOCK_LENGTH;
	retval = edid_buf_size;

fail:
	if (first_edid_block != NULL) {
		kfree(first_edid_block);
		first_edid_block = NULL;
	}

	dpu_pr_info("[DP] edid size is %d", retval);
	return retval;
}

static int dptx_get_first_block_from_callback(struct dp_ctrl *dptx, struct dp_ext_disp_info *dp_attr,
	unsigned int *ext_block_nums)
{
	unsigned int ext_blocks = 0;
	int i;

	memset_s(dptx->edid, DPTX_DEFAULT_EDID_BUFLEN, 0, DPTX_DEFAULT_EDID_BUFLEN);

	for (i = 0; i < DPTX_DEFAULT_EDID_BUFLEN; i++)
		dptx->edid[i] = dp_attr->edid[i];

	if (dptx->edid[EDID_BLOCK_INDEX] > EDID_BLOCK_MAX || dp_attr->edid_blocks != dptx->edid[EDID_BLOCK_INDEX])
		ext_blocks = EDID_BLOCK_DEFAULT;
	else
		ext_blocks = dp_attr->edid_blocks;

	if (ext_blocks > MAX_EXT_BLOCKS || !dptx_check_edid_header(dptx)) {
		if (ext_blocks > MAX_EXT_BLOCKS)
			ext_blocks = MAX_EXT_BLOCKS;
	}
	*ext_block_nums = ext_blocks;

	return 0;
}

static int dptx_read_edid_from_callback(struct dp_ctrl *dptx, struct dp_ext_disp_info *dp_attr)
{
	uint32_t i;
	int retval = 0;
	unsigned int ext_blocks = 0;
	uint8_t *first_edid_block = NULL;
	unsigned int edid_buf_size = 0;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");
	dpu_check_and_return(!dptx->edid, -EINVAL, err, "[DP] dptx->edid is NULL!");

	retval = dptx_get_first_block_from_callback(dptx, dp_attr, &ext_blocks);
	if (retval)
		return retval;

	first_edid_block = kmalloc(EDID_BLOCK_LENGTH, GFP_KERNEL);
	if (first_edid_block == NULL) {
		dpu_pr_err("[DP] Allocate buffer error");
		return -EINVAL;
	}
	memset_s(first_edid_block, EDID_BLOCK_LENGTH, 0, EDID_BLOCK_LENGTH);
	if (memcpy_s(first_edid_block, EDID_BLOCK_LENGTH, dptx->edid, EDID_BLOCK_LENGTH) != EOK) {
		dpu_pr_err("[DP] memcpy edid buffer error!");
		retval = -EINVAL;
		goto fail;
	}
	kfree(dptx->edid);
	dptx->edid = NULL;

	dptx->edid = kzalloc(EDID_BLOCK_LENGTH * ext_blocks + EDID_BLOCK_LENGTH, GFP_KERNEL);
	if (dptx->edid == NULL) {
		dpu_pr_err("[DP] Allocate edid buffer error!");
		retval = -EINVAL;
		goto fail;
	}

	if (memcpy_s(dptx->edid, EDID_BLOCK_LENGTH, first_edid_block, EDID_BLOCK_LENGTH) != EOK) {
		dpu_pr_err("[DP] memcpy edid buffer error!");
		retval = -EINVAL;
		goto fail;
	}
	for (i = EDID_BLOCK_LENGTH; i < EDID_BLOCK_LENGTH * ext_blocks + EDID_BLOCK_LENGTH; i++)
		dptx->edid[i] = dp_attr->edid[i];

	edid_buf_size = EDID_BLOCK_LENGTH * ext_blocks + EDID_BLOCK_LENGTH;
	retval = edid_buf_size;

fail:
	if (first_edid_block != NULL) {
		kfree(first_edid_block);
		first_edid_block = NULL;
	}

	return retval;
}

static int dptx_choose_edid_timing(struct dp_ctrl *dptx, uint8_t *bsafe_mode)
{
	struct edid_timing_info *pref_timing_info = NULL;
	struct dtd *mdtd = NULL;

	mdtd = &dptx->vparams.mdtd;
	if (dptx->edid_info.video.dptx_timing_list == NULL) {
		dptx->error_type = DMD_DP_FAILED_SAFEMODE;
		*bsafe_mode = SAFE_MODE_YES;
		return 0;
	}

	pref_timing_info = dptx_find_default_preferred_timing(dptx);
	if ((pref_timing_info == NULL) || (pref_timing_info->hactive_pixels == SAFE_MODE_TIMING_HACTIVE)) {
		dpu_pr_info("[DP] Do not find preferred timing, set with safe mode");
		dptx->error_type = DMD_DP_FAILED_SAFEMODE;
		*bsafe_mode = SAFE_MODE_YES;
		return 0;
	}
	convert_edid_timing_to_dtd(mdtd, pref_timing_info);
	dptx->vparams.m_fps = (uint8_t)pref_timing_info->fps;
	dptx->max_edid_timing_hactive = pref_timing_info->hactive_pixels;

	dpu_pr_info(
		"[DP] The choosed DTD: pixel_clock is %llu, interlaced is %u, h_active is %u, v_active is %u",
		mdtd->pixel_clock, mdtd->interlaced, mdtd->h_active, mdtd->v_active);
	dpu_pr_info("[DP] DTD pixel_clock: %llu interlaced: %u",
		 mdtd->pixel_clock, mdtd->interlaced);
	dpu_pr_info("[DP] h_active: %hu h_blanking: %hu h_sync_offset: %hu",
		 mdtd->h_active, mdtd->h_blanking, mdtd->h_sync_offset);
	dpu_pr_info("[DP] h_sync_pulse_width: %d h_image_size: %d h_sync_polarity: %d",
		 mdtd->h_sync_pulse_width, mdtd->h_image_size,
		 mdtd->h_sync_polarity);
	dpu_pr_info("[DP] v_active: %d v_blanking: %d v_sync_offset: %d",
		 mdtd->v_active, mdtd->v_blanking, mdtd->v_sync_offset);
	dpu_pr_info("[DP] v_sync_pulse_width: %d v_image_size: %d v_sync_polarity: %d",
		 mdtd->v_sync_pulse_width, mdtd->v_image_size,
		 mdtd->v_sync_polarity);

	dp_imonitor_set_param(DP_PARAM_MAX_WIDTH, &(mdtd->h_active));
	dp_imonitor_set_param(DP_PARAM_MAX_HIGH, &(mdtd->v_active));
	dp_imonitor_set_param(DP_PARAM_PIXEL_CLOCK, &(mdtd->pixel_clock));
	*bsafe_mode = SAFE_MODE_NO;

	return 0;
}

void dptx_link_params_reset(struct dp_ctrl *dptx)
{
	struct dkmd_connector_info *pinfo = NULL;
	dpu_check_and_no_retval(!dptx, err, "[DP] dptx is NULL!");

	pinfo = dptx->connector->conn_info;
	dpu_check_and_no_retval(!pinfo, err, "[DP] pinfo is null pointer");

	dptx->cr_fail = false; /* harutk ---ntr */
	dptx->mst = false;
	dptx->ssc_en = (dptx->connector->conn_info->ssc_dptx_flag != 0) ? true : false;
	dpu_pr_info("[DP] dptx_link_params_reset set ssc_en: %s ", dptx->ssc_en ? "true" : "false");
	dptx->efm_en = true;
	dptx->streams = 1;

	if (pinfo->support_te)
		dptx->dptx_link_params.compress_params.fec = false;
	dptx->is_pps_send_manual = false;
	dptx->dsc = pinfo->dsc_enable;
	dpu_pr_info("[DP] dptx_link_params_reset set fec enable:%d dsc enable %d ",
		dptx->dptx_link_params.compress_params.fec ,dptx->dsc);
	dptx->is_new_frame_update = false;
	dptx->is_need_fix_rate = (bool)pinfo->is_need_fix_rate;
	pinfo->port_id = dptx->port_id;

	dptx_psr_params_reset(dptx);
	dptx_video_params_reset(&dptx->vparams, dptx->bit_depth);

	if (g_dp_debug_mode_enable != 0)
		dptx_debug_set_params(dptx);
}

static int dptx_get_rate_from_reg(uint32_t reg)
{
	switch (reg) {
	case 0x1FA4:
		return DPTX_PHYIF_CTRL_RATE_RBR;
	case 0x34BC:
		return DPTX_PHYIF_CTRL_RATE_HBR;
	case 0x6978:
		return DPTX_PHYIF_CTRL_RATE_HBR2;
	case 0x9E34:
		return DPTX_PHYIF_CTRL_RATE_HBR3;
	case 0x2A30:
		return DPTX_PHYIF_CTRL_EDP_RATE_R216;
	case 0x2F76:
		return DPTX_PHYIF_CTRL_EDP_RATE_R243;
	case 0x3F48:
		return DPTX_PHYIF_CTRL_EDP_RATE_R324;
	case 0x5460:
		return DPTX_PHYIF_CTRL_EDP_RATE_R432;
	default:
		return -1;
	}
}

static int dptx_is_dp_rate(uint32_t reg)
{
	switch (reg) {
	case REG_VALUE_RATE_RBR:
		return DPTX_PHYIF_CTRL_RATE_RBR;
	case REG_VALUE_RATE_HBR:
		return DPTX_PHYIF_CTRL_RATE_HBR;
	case REG_VALUE_RATE_HBR2:
		return DPTX_PHYIF_CTRL_RATE_HBR2;
	case REG_VALUE_RATE_HBR3:
		return DPTX_PHYIF_CTRL_RATE_HBR3;
	default:
		return -1;
	}
}

static bool dptx_check_rx_caps(struct dp_ctrl *dptx)
{
	uint8_t sink_max_lanes;
	uint8_t sink_max_rate;

	if (dptx->is_extended_dpcd_caps_available) {
		if (dptx->dpcd_rev != DP_DPCD_REV_14) {
			dpu_pr_warn("[DP] dptx->dpcd_rev r%x is not supported", dptx->dpcd_rev);
			return false;
		}
	} else {
		if (dptx->dpcd_rev < DP_DPCD_REV_10 || dptx->dpcd_rev > DP_DPCD_REV_14) {
			dpu_pr_warn("[DP] dptx->dpcd_rev r%x is not supported", dptx->dpcd_rev);
			return false;
		}
	}

	sink_max_lanes = dptx_get_sink_max_lane(dptx);
	sink_max_rate = dptx_get_sink_max_rate(dptx);

	dpu_pr_info("[DP] sink max lane is %u, sink max rate is %u ", sink_max_lanes, sink_max_rate);
	if (sink_max_lanes == DPTX_PHYIF_MAX_LANE_COUNT_INVALID || sink_max_rate == DPTX_PHYIF_CTRL_RATE_INVALID)
		return false;

	return true;
}

static bool dptx_get_receiver_capabiliity_from_dpcd(struct dp_ctrl *dptx)
{
	int retval;
	uint8_t read_dpcd_retry_num = 0;
	uint8_t read_dpcd_retry_max_num = 3;
    uint8_t down_stream_port_present = 0;
    uint8_t byte[1];
	int i;

	dptx->is_extended_dpcd_caps_available = false;
	while (read_dpcd_retry_num <= read_dpcd_retry_max_num) {
		if ((++read_dpcd_retry_num) > read_dpcd_retry_max_num) {
			dpu_pr_err("[DP] failed to dptx_read_bytes_from_dpcd DP_DPCD_REV");
			return false;
		}

		if (memset_s(dptx->rx_original_caps, DPTX_RECEIVER_CAP_SIZE, 0, DPTX_RECEIVER_CAP_SIZE) != 0)
			dpu_pr_warn("[DP] rx_original_caps memset_s failed");

		retval = dptx_read_bytes_from_dpcd(dptx, DP_DPCD_REV, dptx->rx_original_caps, DPTX_RECEIVER_CAP_SIZE);
		if (retval != 0) {
			dpu_pr_warn("[DP] failed to read DP_DPCD_REV, retrynum is %u", read_dpcd_retry_num);
			continue;
		}
		dptx->dpcd_rev = dptx->rx_original_caps[DP_DPCD_REV];

		// DPCD Addresses 02200h through 022FFh is valid with DPCD r1.4(and higher)
		if ((dptx->rx_original_caps[DP_TRAINING_AUX_RD_INTERVAL] & DP_EXTENDED_RECEIVER_CAPABILITY_FIELD_PRESENT) != 0) {
			dpu_pr_info("[DP] Extended Receiver Capability field should present");
			if (memset_s(dptx->rx_extended_caps, DPTX_RECEIVER_CAP_SIZE, 0, DPTX_RECEIVER_CAP_SIZE) != 0)
				dpu_pr_warn("[DP] rx_extended_caps memset_s failed");
			retval = dptx_read_bytes_from_dpcd(dptx, DP_DPCD_REV_EXT, dptx->rx_extended_caps, DPTX_RECEIVER_CAP_SIZE);
			if (retval != 0) {
				dpu_pr_warn("[DP] failed to read DP_DPCD_REV_EXT, retrynum is %u", read_dpcd_retry_num);
				continue;
			}
			dptx->dpcd_rev = dptx->rx_extended_caps[DP_DPCD_REV];
			dptx->is_extended_dpcd_caps_available = true;
		}
		if (g_dp_debug_cts_mode != 0) {
			down_stream_port_present = (dptx->is_extended_dpcd_caps_available ? dptx->rx_extended_caps[5] :
				dptx->rx_original_caps[5]); // DOWN_STREAM_PORT_PRESENT 05h
			if ((down_stream_port_present & BIT(0)) != 0x0) {
				dpu_pr_info("get branch cap");
				for(i = DP_DOWNSTREAM_PORT_0; i < 0x90; i++) {
					dptx_read_bytes_from_dpcd(dptx, i, byte, 1);
					dpu_pr_info("[DP] DPCD_ADDR:0x%x, value:0x%x", i, byte[0]);
					byte[0] = 0;
				}
			}
		}

		dpu_pr_info("[DP] DPCD Revision:r%x", dptx->dpcd_rev);

		dp_imonitor_set_param(DP_PARAM_DPCD_RX_CAPS, dptx->rx_original_caps);
		if (dptx_check_rx_caps(dptx))
			break;
	}
	return true;
}

static int dptx_get_device_caps(struct dp_ctrl *dptx)
{
	uint32_t rate_reg = 0;
	int i;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");
	if (!dptx_get_receiver_capabiliity_from_dpcd(dptx))
		return -EINVAL;

	// read dpcd: 0x10 0x11 ... 0x1F, get rate RX suported;
	memset_s(dptx->rx_rate_caps, sizeof(uint32_t) * DPTX_RATE_CAP_SIZE, 0, sizeof(uint32_t) * DPTX_RATE_CAP_SIZE);
	dptx->rx_rate_count = 0;
	dptx->rx_rate_index = 0;
	for (i = 0; i < DPTX_RATE_CAP_SIZE; i++) {
		rate_reg = dptx->rx_original_caps[DPTX_RATE_CAP_SHIFT + (i * 2) + 1] << DPTX_RATE_BYTE_WIDTH |
			dptx->rx_original_caps[DPTX_RATE_CAP_SHIFT + (i * 2)];
		if (dptx_get_rate_from_reg(rate_reg) >= 0 &&
				(dptx->connector->conn_info->base.mode == EDP_MODE || dptx_is_dp_rate(rate_reg) >= 0)) {
			dptx->rx_rate_caps[dptx->rx_rate_count] = (uint32_t)dptx_get_rate_from_reg(rate_reg);
			dptx->rx_rate_count++;
			dpu_pr_info("[DP] [%d-%u] sink rate: %x, rate_reg: 0x%x", i, dptx->rx_rate_count,
			dptx->rx_rate_caps[dptx->rx_rate_count - 1], rate_reg);
		} else {
			dpu_pr_info("[DP] [%d-%u] rate_reg invalid : 0x%x, mode: %d",
				i, dptx->rx_rate_count, rate_reg, dptx->connector->conn_info->base.mode);
		}
	}
	// if rx not set 0x10~0x1F, set default values.
	if (dptx->rx_rate_count <= 0) {
		dptx->rx_rate_count = 4;
		if (dptx->connector->conn_info->base.mode == EDP_MODE) {
			dpu_pr_info("[DP] set edp default sink rate, size: %u", dptx->rx_rate_count);
			dptx->rx_rate_caps[0] = DPTX_PHYIF_CTRL_EDP_RATE_R216;
			dptx->rx_rate_caps[1] = DPTX_PHYIF_CTRL_EDP_RATE_R243;
			dptx->rx_rate_caps[2] = DPTX_PHYIF_CTRL_EDP_RATE_R324;
			dptx->rx_rate_caps[3] = DPTX_PHYIF_CTRL_EDP_RATE_R432;
		} else {
			dpu_pr_info("[DP] set dp default sink rate, size: %u", dptx->rx_rate_count);
			dptx->rx_rate_caps[0] = DPTX_PHYIF_CTRL_RATE_RBR;
			dptx->rx_rate_caps[1] = DPTX_PHYIF_CTRL_RATE_HBR;
			dptx->rx_rate_caps[2] = DPTX_PHYIF_CTRL_RATE_HBR2;
			dptx->rx_rate_caps[3] = DPTX_PHYIF_CTRL_RATE_HBR3;
		}
	}
	if (dptx_link_rate_index(dptx, (uint8_t)dptx->connector->conn_info->min_dptx_rate) < 0) {
		dpu_pr_warn("[DP] invalid min_dptx_rate, set to caps[0]. pre min=%d",
			dptx->connector->conn_info->min_dptx_rate);
		dptx->connector->conn_info->min_dptx_rate = dptx->rx_rate_caps[0];
	}

	return 0;
}

static int dptx_link_psr_init(struct dp_ctrl *dptx)
{
	int retval;
	int reg = 0;

	if (dptx->connector->conn_info->base.mode == EDP_MODE) {
		retval = dptx_get_psr_cap(dptx);
		if (retval) {
			dpu_pr_err("[DP] Get psr capability fail");
			return retval;
		}
		if (dptx->psr_params.psr_version >= DP_PSR_IS_SUPPORTED)
			dptx_psr_intr_en(dptx);

		if (g_dp_debug_mode_enable) {
			reg = dp_debug_get_psr_state();
			dpu_pr_info("[DP] psr_state: %d\n", reg);
		}

        if ((reg == DP_PSR_IS_SUPPORTED || dptx->psr_params.psr_version == DP_PSR_IS_SUPPORTED) &&
            reg != DP_PSR2_IS_SUPPORTED) {
            retval = dptx_psr_initial(dptx);
			if (retval != 0) {
				dpu_pr_err("[DP] Init psr fail");
				return retval;
			}
			dpu_pr_info("[DP] psr init");
        } else if (reg == DP_PSR2_IS_SUPPORTED || dptx->psr_params.psr_version >= DP_PSR2_IS_SUPPORTED) {
            retval = dptx_psr2_initial(dptx);
			if (retval != 0) {
				dpu_pr_err("[DP] Init psr2 fail");
				return retval;
			}
			dpu_pr_info("[DP] psr2 init");
        } else {
            dpu_pr_info("[DP] psr2/psr NOT SUPPORTED, psr_version:%u\n", dptx->psr_params.psr_version);
        }
    }
	return 0;
}

static int dptx_link_get_device_caps(struct dp_ctrl *dptx)
{
	int retval;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	/* set sink device power state to D0 */
	retval = dptx_write_dpcd(dptx, DP_SET_POWER, DP_SET_POWER_D0);
	if (retval != 0) {
		dp_maintenance_add_error_log(dptx, "failed_write_POWER");
		dpu_pr_warn("[DP] failed to dptx_write_dpcd DP_SET_POWER, DP_SET_POWER_D0 %d", retval);
		return retval;
	}
	mdelay(1);

	/* get rx_caps */
	retval = dptx_get_device_caps(dptx);
	if (retval != 0) {
		dpu_pr_warn("[DP] Check device capability failed");
		return retval;
	}

	/* get mst capability, info in rx_caps */
	retval = dptx_mst_initial(dptx);
	if (retval != 0) {
		dpu_pr_warn("[DP] Failed to get mst info %d", retval);
		return retval;
	}

	/* get dsc capability, info in rx_caps */
	retval = dptx_dsc_initial(dptx);
	if (retval != 0) {
		dpu_pr_warn("[DP] Failed to get dsc info %d", retval);
		return retval;
	}

	return 0;
}

static int dptx_get_test_request(struct dp_ctrl *dptx)
{
	int retval;
	uint8_t blocks = 0;
	uint8_t test_request = 0;
	uint8_t vector = 0;
	uint8_t checksum = 0;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");

	retval = dptx_read_dpcd(dptx, DP_DEVICE_SERVICE_IRQ_VECTOR, &vector);
	if (retval) {
		dpu_pr_err("[DP] failed to dptx_read_dpcd DP_DEVICE_SERVICE_IRQ_VECTOR, retval=%d", retval);
		return retval;
	}

	if (vector & DP_AUTOMATED_TEST_REQUEST) {
		dpu_pr_info("[DP] DP_AUTOMATED_TEST_REQUEST");
		retval = dptx_read_dpcd(dptx, DP_TEST_REQUEST, &test_request);
		if (retval) {
			dpu_pr_err("[DP] failed to dptx_read_dpcd DP_TEST_REQUEST, retval=%d", retval);
			return retval;
		}

		if (test_request & DP_TEST_LINK_EDID_READ) {
			blocks = dptx->edid[126];
			checksum = dptx->edid[127 + EDID_BLOCK_LENGTH * blocks];

			retval = dptx_write_dpcd(dptx, DP_TEST_EDID_CHECKSUM, checksum);
			if (retval) {
				dpu_pr_err(
					"[DP] failed to dptx_write_dpcd DP_TEST_EDID_CHECKSUM, retval=%d", retval);
				return retval;
			}

			retval = dptx_write_dpcd(dptx, DP_TEST_RESPONSE, DP_TEST_EDID_CHECKSUM_WRITE);
			if (retval) {
				dpu_pr_err("[DP] failed to dptx_write_dpcd DP_TEST_RESPONSE, retval=%d", retval);
				return retval;
			}
		}
	}

	return 0;
}

/**
 * default link params and controller reset
 * reset mst/fec/dec control params here
 */
static int dptx_link_reset(struct dp_ctrl *dptx)
{
	struct dtd *mdtd = NULL;
	struct video_params *vparams = NULL;

	dptx_link_params_reset(dptx);

	vparams = &dptx->vparams;
	mdtd = &vparams->mdtd;

	if (!convert_code_to_dtd(mdtd, vparams->mode, vparams->refresh_rate, vparams->video_format))
		return -EINVAL;

	if (dptx->dptx_video_core_config)
		dptx->dptx_video_core_config(dptx, 0);

	if (dptx->dptx_link_core_reset)
		dptx->dptx_link_core_reset(dptx);

	return 0;
}

/**
 * get sink device's edid, now just for sst
 * include mst mode which is now implemented in dptx_get_topology
 */
int dptx_link_get_device_edid(struct dp_ctrl *dptx, uint8_t *bsafe_mode, uint32_t *edid_info_size)
{
	int retval;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");
	dpu_check_and_return(!bsafe_mode, -EINVAL, err, "[DP] bsafe_mode is NULL!");
	dpu_check_and_return(!edid_info_size, -EINVAL, err, "[DP] edid_info_size is NULL!");

	*bsafe_mode = SAFE_MODE_NO;
	if (dptx->mst) {
		retval = dptx_get_topology(dptx);
		if (retval)
			return retval;
	}

	if (!dptx->mst) {
		/* callback from dprx, such as 983, not support edid info */
		if (dptx->dp_info && dptx->dp_info->is_dprx_ready && dptx->dp_info->get_ext_disp_info != NULL) {
			retval = dptx_get_ext_disp_info(dptx);
			if (retval != 0) {
				dpu_pr_err("[DP] set ext disp info error\n");
				return -EINVAL;
			}
			retval = dptx_read_edid_from_callback(dptx, dptx->dp_ext_info);
		} else {
			retval = dptx_read_edid(dptx);
		}
		if (retval == -ETIMEDOUT) {
			dp_imonitor_set_param(DP_PARAM_READ_EDID_FAILED, &retval);
			*edid_info_size = 0;
			return retval;
		} else if (retval < EDID_BLOCK_LENGTH) {
			dpu_pr_err("[DP] failed to dptx_read_edid, retval=%d, dptx->port_id=%d", retval, dptx->port_id);
			dp_imonitor_set_param(DP_PARAM_READ_EDID_FAILED, &retval);
			*edid_info_size = 0;
			dp_maintenance_add_error_log(dptx, "failed rd edid safe");
			dptx->error_type = DMD_DP_FAILED_SAFEMODE;
			*bsafe_mode = SAFE_MODE_YES;
			return retval;
		} else {
			dpu_pr_info("[DP] success to dptx_read_edid, retval=%d, dptx->port_id=%d", retval, dptx->port_id);
			*edid_info_size = retval;
		}

		dptx->connector->conn_info->base.edid = dptx->edid;
		dpu_pr_info("[DP] EDID parse_edid");
		retval = parse_edid(dptx, *edid_info_size);
		if (retval) {
			dpu_pr_err("[DP] EDID Parser fail, display safe mode");
			dp_maintenance_add_error_log(dptx, "EDID fail safe mode");
			dptx->error_type = DMD_DP_FAILED_SAFEMODE;
			*bsafe_mode = SAFE_MODE_YES;
			return retval;
		}
	}

	/**
	 * The TEST_EDID_READ is asserted on HOTPLUG. Check for it and
	 * handle it here.
	 */
	retval = dptx_get_test_request(dptx);
	if (retval) {
		dpu_pr_err("[DP] Check test request failed");
		return retval;
	}

	return 0;
}

static void dptx_debug_set_clk_hblank_params(struct dp_ctrl *dptx, struct video_params *vparams, struct dtd *mdtd)
{
	uint32_t video_code = 0;

	if ((g_dp_debug_mode_enable != 0) && dp_debug_get_clk_hblank_enable(dptx)) {
		dp_debug_get_clk_hblank_params(dptx, &(mdtd->pixel_clock), &video_code, &(mdtd->h_blanking));
		vparams->video_format = CTA;
		(void)convert_code_to_dtd(mdtd, video_code, vparams->refresh_rate, vparams->video_format);
		if (memcpy_s(&(dptx->vparams.mdtd), sizeof(dptx->vparams.mdtd), mdtd, sizeof(dptx->vparams.mdtd)) != EOK)
			dpu_pr_err("[DP] memcpy_s err");
	}
}

static int dptx_set_default_resolution(struct dp_ctrl *dptx, uint8_t bsafe_mode, int edid_info_size, struct dtd *mdtd)
{
	struct video_params *vparams = NULL;
	uint8_t rev = 0;
	int retval = 0;
	uint32_t safe_mode_vic1 = 1;
	uint32_t safe_mode_vic2 = 16;
	vparams = &dptx->vparams;
	vparams->video_format = CTA;
	dp_imonitor_set_param(DP_PARAM_SAFE_MODE, &bsafe_mode);
	if (edid_info_size != 0 || g_out_adpative_sync != 0) {
		/* If edid is parsed error, DP transfer 640*480 firstly! */
		(void)convert_code_to_dtd(mdtd, safe_mode_vic1, vparams->refresh_rate, vparams->video_format);
	} else {
		/* If edid can't be got, DP transfer 1920*1080 firstly! */
		(void)convert_code_to_dtd(mdtd, safe_mode_vic2, vparams->refresh_rate, vparams->video_format);
		retval = dptx_read_dpcd(dptx, DP_DOWNSTREAMPORT_PRESENT, &rev);
		if (retval) {
			dpu_pr_err("[DP] failed to dptx_read_dpcd DP_DOWNSTREAMPORT_PRESENT, retval=%d", retval);
			return retval;
		}

		if (((rev & DP_DWN_STRM_PORT_TYPE_MASK) >> 1) != 0x01) {
			dptx->edid_info.audio.basic_audio = 0x1;
			dpu_pr_info("[DP] If DFP port don't belong to analog(VGA/DVI-I), update audio capabilty");
			dp_imonitor_set_param(DP_PARAM_BASIC_AUDIO, &(dptx->edid_info.audio.basic_audio));
		}
	}

	if (dptx->connector->conn_info->base.fpga_flag) {
		vparams->video_format = CTA;
		/* Fpga default display 720*480 */
		(void)convert_code_to_dtd(mdtd, g_video_code, vparams->refresh_rate, vparams->video_format);
		if (g_pixel_clock)
			mdtd->pixel_clock = g_pixel_clock;
		mdtd->h_blanking = g_hblank;
	}
	if (memcpy_s(&(dptx->vparams.mdtd), sizeof(dptx->vparams.mdtd), mdtd, sizeof(*mdtd)) != EOK) {
		dpu_pr_err ("[DP] memcpy mdtd error");
		return -EINVAL; 
	}
	return 0;
}

int dptx_link_choose_timing(struct dp_ctrl *dptx, uint8_t bsafe_mode, int edid_info_size)
{
	struct video_params *vparams = NULL;
	struct dtd mdtd;
	int retval = 0;

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");
	if (g_dp_set_safemode && dptx->connector->conn_info->base.mode != EDP_MODE) {
		dpu_pr_warn("[DP] set safe mode for port:%d", dptx->port_id);
		dptx->error_type = DMD_DP_FAILED_SAFEMODE;
		dp_maintenance_add_error_log(dptx, "set safe mode");
		bsafe_mode = SAFE_MODE_YES;
	}

	vparams = &dptx->vparams;
	if (bsafe_mode == SAFE_MODE_NO)
		dptx_choose_edid_timing(dptx, &bsafe_mode);

	if ((bsafe_mode == SAFE_MODE_YES) || (dptx->connector->conn_info->base.fpga_flag != 0)) {
		dpu_pr_info("[DP] bsafe_mode:%u fpga_flag : %d",bsafe_mode, dptx->connector->conn_info->base.fpga_flag);
		retval = dptx_set_default_resolution(dptx, bsafe_mode, edid_info_size, &mdtd);
		if (retval) {
			dpu_pr_err("[DP] to set default resolution failed");
			return retval;
		}
	}

	if (g_clk_hblank_code_enable) {
		vparams->video_format = CTA;
		(void)convert_code_to_dtd(&mdtd, g_video_code, vparams->refresh_rate, vparams->video_format);
		if (g_pixel_clock)
			mdtd.pixel_clock = g_pixel_clock;
		mdtd.h_blanking = g_hblank;
		memcpy(&(dptx->vparams.mdtd), &mdtd, sizeof(mdtd));
	}

	dptx_debug_set_clk_hblank_params(dptx, vparams, &mdtd);

	/* change video display timing after select default perferred timing */
	dptx_change_video_timing(dptx);

	if (dptx->dptx_video_ts_calculate)
		retval = dptx->dptx_video_ts_calculate(dptx, dptx->link.lanes, dptx->link.rate,
			vparams->bpc, vparams->pix_enc, vparams->mdtd.pixel_clock);
	if (retval)
		dpu_pr_info("[DP] Can't change to the preferred video mode: frequency=%llu", vparams->mdtd.pixel_clock);
	else
		dpu_pr_info("[DP] pixel_frequency=%llu", vparams->mdtd.pixel_clock);

	/* Enable DSC or not according to the tu bandwith */
	dptx_dsc_enabled_by_tu(dptx);
	dptx_get_tu_and_bpc_value(dptx);

	return 0;
}

int handle_hotplug(struct dp_ctrl *dptx)
{
	int retval;
	int retry = 0;
	struct dkmd_connector_info *conn_info = NULL;

	dpu_pr_info("[DP] +");

	dpu_check_and_return(!dptx, -EINVAL, err, "[DP] dptx is NULL!");
	dpu_check_and_return(!dptx->dptx_enable, -EINVAL, err, "[DP] dptx has already off!");

	dp_imonitor_set_param(DP_PARAM_TIME_START, NULL);
	conn_info = dptx->connector->conn_info;

	dpu_check_and_return(!conn_info, -EINVAL, err, "[DP] conn_info is NULL!");

	/**
	 * default link params and controller reset
	 * reset mst/fec/dec control params here
	 */
	retval = dptx_link_reset(dptx);
	if (retval)
		return retval;

	do {
		retry++;
		/* get rx_caps */
		retval = dptx_link_get_device_caps(dptx);
		/* detect link timeout */
		if (dptx->is_link_timeout) {
			dpu_pr_info("[DP] DP link is timeout!");
			return -ETIMEDOUT;
		}
		if ((retval != 0) && dptx->dp_info && dptx->dp_info->is_dprx_ready &&
				(dptx->dp_info->dp_connect_ctrl_reset_dprx != NULL)) {
			/* reset dprx of 983 */
			dpu_pr_err("[DP] dp_connect_ctrl_reset_dprx entry");
			dptx->dp_info->dp_connect_ctrl_reset_dprx(dptx->port_id);
		}
		if ((retry > GET_DEVICE_CAPS_TIMES) && (retval != 0)) {
			dpu_pr_err("[DP] failed to get_device_caps, retry: %d, retval: %d", retry, retval);
			dp_maintenance_add_error_log(dptx, "failed getdevicecaps");
			dptx->error_type = DMD_DP_FAILED_AUXERROR;
			return retval;
		}
		if (retval != 0) {
			dpu_pr_info("[DP] dptx_link_get_device_caps faild, time is %d, retry get device caps", retry);
			mdelay(GET_DEVICE_CAPS_INTERVAL);
		}
	} while (retval);

	dpu_pr_info("[DP] handle_hotplug rate=%u, lanes=%u", dptx->max_rate, dptx->max_lanes);

	if (g_dp_debug_jump_linktraining_enable) {
		dpu_pr_warn("[DP] g_dp_debug_jump_linktraining_enable enable");
		return -ECONNREFUSED;
	}

	/* No other IRQ should be set on hotplug */
	retval = dptx_link_retraining(dptx, dptx->max_rate, dptx->max_lanes);
	if (retval) {
		dpu_pr_err("[DP] failed to dptx_link_training, retval=%d", retval);
		dp_maintenance_add_error_log(dptx, "failed linktraining");
		dptx->error_type = dptx->error_type == DMD_DP_SUCCESS ? DMD_DP_FAILED_LTERROR : dptx->error_type;
		dp_imonitor_set_param(DP_PARAM_LINK_TRAINING_FAILED, &retval);
		return retval;
	}
	msleep(1);

	/* enable CRC caps */
	dptx_enable_crc(dptx);

	/* PSR mode config */
	retval = dptx_link_psr_init(dptx);
	if (retval) {
		dpu_pr_err("[DP] Failed to init psr %d", retval);
		dp_maintenance_add_error_log(dptx, "Failed to init psr");
		dptx->error_type = DMD_DP_FAILED_PSRINITERROR;
		return retval;
	}

	if (conn_info->support_te && conn_info->base.mode == EDP_MODE)
		dptx_psr2_params_init(dptx);

	/* eDP backlight mode config */
	if (conn_info->base.mode == EDP_MODE)
		dptx->dptx_aux_set_backlight_config(dptx);

	dpu_pr_info("handle_hotplug g_enable_adpative_sync = %d \n", g_enable_adpative_sync);
	if (g_enable_adpative_sync)
		 config_adaptive_sync(dptx, g_max_line_cnt);

	/* config dss and dp core */
	if (dptx->dptx_link_timing_config)
		retval = dptx->dptx_link_timing_config(dptx);
	if (retval) {
		dptx->error_type = DMD_DP_FAILED_CFGTIMEERROR;
		return retval;
	}

	if (g_enable_adpative_sync)
		dptx_init_detect_work(dptx);

	dptx->current_link_rate = dptx->link.rate;
	dptx->current_link_lanes = dptx->link.lanes;

	/* for factory test */
	if (dp_factory_mode_is_enable()) {
		if (!dp_factory_is_4k_60fps(dptx->max_rate, dptx->max_lanes,
			dptx->vparams.mdtd.h_active, dptx->vparams.mdtd.v_active, dptx->vparams.m_fps)) {
			dpu_pr_err("[DP] can't hotplug when combinations is invalid in factory mode!");
			if (dptx->edid_info.audio.basic_audio == 0x1)
				switch_set_state(&dptx->dp_switch, 0);
			dptx->error_type = DMD_DP_FAILED_FCTERROR;
			return -ECONNREFUSED;
		}
	}

	dpu_pr_info("[DP] current_link_rate=%d, current_link_lanes=%d -",
		dptx->current_link_rate, dptx->current_link_lanes);

	return 0;
}
