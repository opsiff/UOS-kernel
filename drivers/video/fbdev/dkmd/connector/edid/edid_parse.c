/**
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

#include "edid_private.h"
#include "dkmd_log.h"
#include "securec.h"
#include "edid_timing_select.h"
#include "dkmd_edid_interface.h"
#include "edid_timing_config.h"

static struct list_head *g_hdmi_vic_list = NULL;
static uint8_t g_hdmi_vic_len = 0;
static uint8_t g_hdmi_vic_real_len = 0;

enum video_format_type {
    CTA = 0,
    DMT = 2
};

static struct vary_vfp_data hm_vary_vfp_tlb[] = {
    {  8, 1, {{59940, 22, 4}, {    0,  0, 0}} },
    {  9, 1, {{59940, 22, 4}, {    0,  0, 0}} },
    { 12, 1, {{60054, 22, 4}, {    0,  0, 0}} },
    { 13, 1, {{60054, 22, 4}, {    0,  0, 0}} },
    { 23, 2, {{50080, 24, 2}, {49920, 25, 3}} },
    { 24, 2, {{50080, 24, 2}, {49920, 25, 3}} },
    { 27, 2, {{50080, 24, 2}, {49920, 25, 3}} },
    { 28, 2, {{50080, 24, 2}, {49920, 25, 3}} }
};

static const struct image_ration_t image_aspect_ration[] = {
	{.h_ratio = 16, .v_ratio = 10},
	{.h_ratio = 4, .v_ratio = 3},
	{.h_ratio = 5, .v_ratio = 4},
	{.h_ratio = 16, .v_ratio = 9},
};

#define IMAGE_H_RATIO(x)	image_aspect_ration[IMAGE_ASPECT_RATIO_INDEX(x)].h_ratio
#define IMAGE_V_RATIO(x)	image_aspect_ration[IMAGE_ASPECT_RATIO_INDEX(x)].v_ratio

static int check_main_edid(const uint8_t *edid)
{
    uint32_t checksum = 0;
    int32_t i = 0;

    /* Verify 0 checksum */
    for (i = 0; i < EDID_LENGTH; i++)
        checksum += edid[i];
    if (checksum & 0xFF) {
        dpu_pr_warn("[EDID] EDID checksum failed - data is corrupt. Continuing anyway.");
        return -EINVAL;
    }

    /* Verify Header */
    for (i = 0; i < EDID_HEADER_END + 1; i++) {
        if (edid[i] != edid_v1_header[i]) {
            dpu_pr_warn("[EDID] first uint8_ts don't match EDID version 1 header");
            return -EINVAL;
        }
    }

    return 0;
}

static int edid_calculate_fps(struct edid_timing_info *timing_info, uint16_t *fps)
{
	uint64_t pixels = 0;
	uint64_t pixel_clock = 0;

	pixels = ((timing_info->hactive_pixels + timing_info->hblanking) *
		(timing_info->vactive_pixels + timing_info->vblanking));
	if (pixels == 0) {
		dpu_pr_info("[EDID] pixels cannot be zero");
		return -EINVAL;
	}
	pixel_clock = timing_info->pixel_clock * 1000; // unit:MHz
	*fps = (uint16_t)(pixel_clock / pixels);
	if (VALUE_DIFF(*fps, PREFERRED_DEFAULT_FPS) == 1)
		*fps = PREFERRED_DEFAULT_FPS;
	if (VALUE_DIFF(*fps, SUPPORT_FPS1) == 1)
		*fps = SUPPORT_FPS1;

	dpu_pr_info("[EDID] edid_calculate fps:%u, pixels:%u, pixel_clock:%llu", *fps, pixels, timing_info->pixel_clock);

	return 0;
}

static void parse_priority_timing_description(struct edid_video_timing *vid_info, struct edid_timing_node *node,
    bool is_native)
{
    /* Add only the first resolution or the resolution marked as native. */
    if ((vid_info->main_vcount == 1 || is_native) && vid_info->pri_vcount < PRIORITY_TIMING_MAX) {
        vid_info->pri_timing[vid_info->pri_vcount].pri_hpixels = node->timing_info.hactive_pixels;
        vid_info->pri_timing[vid_info->pri_vcount].pri_vpixels = node->timing_info.vactive_pixels;
        vid_info->pri_timing[vid_info->pri_vcount].pri_fps = node->timing_info.fps;
        dpu_pr_info("[EDID] add priority timing pri_hpixels: %hu pri_vpixels: %hu pri_fps: %hu pri_vcount: %hhu",
            vid_info->pri_timing[vid_info->pri_vcount].pri_hpixels,
            vid_info->pri_timing[vid_info->pri_vcount].pri_vpixels,
            vid_info->pri_timing[vid_info->pri_vcount].pri_fps, vid_info->pri_vcount);
        vid_info->pri_vcount++;
    } else {
        dpu_pr_info("[EDID] Not add priority timing main_vcount: %hhu is_native: %d pri_vcount: %hhu",
            vid_info->main_vcount, is_native, vid_info->pri_vcount);
    }
}

static int parse_video_timing_description(struct edid_video_timing *vid_info, const uint8_t *dtd, const uint8_t *edid)
{
    struct edid_timing_node *node = NULL;

    dpu_pr_info("[EDID] parse_video_timing_description start !");

    /* Get Max Value by comparing all values */
    if ((vid_info->max_hpixels < H_ACTIVE(dtd)) && (vid_info->max_vpixels <= V_ACTIVE(dtd)) &&
        (vid_info->max_pixel_clock < PIXEL_CLOCK(dtd) * 10)) {
        vid_info->max_hpixels = H_ACTIVE(dtd);
        vid_info->max_vpixels = V_ACTIVE(dtd);
        vid_info->max_pixel_clock = PIXEL_CLOCK(dtd) * 10;
        dpu_pr_info("[EDID] max_pixel_clock 2 is %llu", vid_info->max_pixel_clock);
    }

    node = kzalloc(sizeof(struct edid_timing_node), GFP_KERNEL);
    if (node != NULL) {
        node->timing_info.hactive_pixels = H_ACTIVE(dtd);
        node->timing_info.hblanking = H_BLANKING(dtd);
        node->timing_info.hsync_offset = H_SYNC_OFFSET(dtd);
        node->timing_info.hsync_pulse_width = H_SYNC_WIDTH(dtd);
        node->timing_info.hborder = H_BORDER(dtd);
        node->timing_info.hsize = H_SIZE(dtd);

        node->timing_info.vactive_pixels = V_ACTIVE(dtd);
        node->timing_info.vblanking = V_BLANKING(dtd);
        node->timing_info.vsync_offset = V_SYNC_OFFSET(dtd);
        node->timing_info.vsync_pulse_width = V_SYNC_WIDTH(dtd);
        node->timing_info.vborder = V_BORDER(dtd);
        node->timing_info.vsize = V_SIZE(dtd);

        node->timing_info.pixel_clock = PIXEL_CLOCK(dtd) * 10; // unit:KHz

        node->timing_info.input_type = INPUT_TYPE(dtd); // need to modify later
        node->timing_info.interlaced = VSCAN_MODE(dtd);
        node->timing_info.vsync_polarity = V_SYNC_POLARITY(dtd);
        node->timing_info.hsync_polarity = H_SYNC_POLARITY(dtd);
        node->timing_info.sync_scheme = SYNC_SCHEME(dtd);
        node->timing_info.scheme_detail = SCHEME_DETAIL(dtd);

		if (edid_calculate_fps(&node->timing_info, &node->timing_info.fps) != 0) {
            kfree(node);
            return -EINVAL;
        }

		if (dtd == (edid + DETAILED_TIMING_DESCRIPTIONS_START)) {
			node->timing_info.preferred = true;
			dpu_pr_info("[EDID] The timinginfo[%hhu] [%u * %u @ %uHz]is EDID preferred timing mode",
				vid_info->main_vcount, node->timing_info.hactive_pixels, node->timing_info.vactive_pixels,
				node->timing_info.fps);
		} else {
			node->timing_info.preferred = false;
		}

        vid_info->main_vcount += 1;

        dpu_pr_info("main_vcount: %hu hactive_pixels: %hu hblanking: %hu hsync_offset: %hu \
            hsync_pulse_width: %hu hborder: %hu hsize: %hu", vid_info->main_vcount, node->timing_info.hactive_pixels,
            node->timing_info.hblanking, node->timing_info.hsync_offset, node->timing_info.hsync_pulse_width,
			node->timing_info.hborder, node->timing_info.hsize);
        dpu_pr_info("vactive_pixels: %hu vblanking: %hu vsync_offset: %hu vsync_pulse_width: %hu \
            vborder: %hu vsize: %hu pixel_clock: %llu", node->timing_info.vactive_pixels, node->timing_info.vblanking,
            node->timing_info.vsync_offset, node->timing_info.vsync_pulse_width, node->timing_info.vborder,
			node->timing_info.vsize, node->timing_info.pixel_clock);

        list_add_tail(&node->list_node, &vid_info->video_timing_list);
        parse_priority_timing_description(vid_info, node, false);
    } else {
        dpu_pr_err("[EDID] kzalloc struct hdmi_vic fail!");
        return -EINVAL;
    }

    dpu_pr_info("[EDID] The timinginfo %hhu: hactive_pixels: %hu, vactive_pixels: %hu fps: %u pixel clock: %llu",
        vid_info->main_vcount, node->timing_info.hactive_pixels, node->timing_info.vactive_pixels,
		node->timing_info.fps, node->timing_info.pixel_clock);

    return 0;
}

static void parse_monitor_video_limits(struct edid_video_timing *video_timing, const uint8_t *block)
{
    dpu_pr_info("[EDID] parse_monitor_video_limits start !");

    /* Set up limit fields */
    video_timing->max_hfreq = H_MAX_RATE(block);
    video_timing->min_hfreq = H_MIN_RATE(block);
    video_timing->max_vfreq = V_MAX_RATE(block);
    video_timing->min_vfreq = V_MIN_RATE(block);
    video_timing->max_pixel_clock = MAX_PIXEL_CLOCK(block) * 1000; // Convert to KHz

	video_timing->video_timing_support_flag = VIDEO_TIMING_SUPPORT_FLAG(block);
	if (video_timing->video_timing_support_flag == SECONDARY_GTF_SUPPORTED) {
		dpu_pr_info("[EDID] video_timing_support_flag is gtf2");
		video_timing->gtf2_start_freq = GTF2_START_FREQ(block);
		video_timing->gtf2_c = GTF2_C(block);
		video_timing->gtf2_m = GTF2_M(block);
		video_timing->gtf2_k = GTF2_K(block);
		video_timing->gtf2_j = GTF2_J(block);
	}

    dpu_pr_info("[EDID] parse_monitor_video_limits max_pixel_clock is %llu", video_timing->max_pixel_clock);
    dpu_pr_info("[EDID] parse_monitor_video_limits: max_hfreq=%hu min_hfreq=%hu max_vfreq=%hu min_vfreq=%hu",
        video_timing->max_hfreq, video_timing->min_hfreq, video_timing->max_vfreq, video_timing->min_vfreq);
}

static int parse_monitor_name(struct edid_video_timing *video_timing, const uint8_t *blockname, uint32_t size)
{
    uint32_t i = 0;
    uint32_t str_start = 5;
    uint32_t str_end = 0;

    for (i = str_start; i < (size - 1); i++) {
        if ((blockname[i] == 0x0A) && (blockname[i + 1] == 0x20)) {
            str_end = i;
            break;
        }
    }

    if (str_end == 0)
        str_end = size;

    if (str_end < str_start) {
        dpu_pr_err("[EDID] The length of monitor name is wrong");
        return -EINVAL;
    }

    if (((str_end - str_start) < MONITOR_NAME_DESCRIPTION_SIZE)) {
        if (memcpy_s(video_timing->monitor_descriptor, MONITOR_NAME_DESCRIPTION_SIZE,
            &blockname[str_start], str_end - str_start) != EOK) {
            dpu_pr_err("[EDID] Failed to memcpy monitor name");
            return -EINVAL;
        }
        video_timing->monitor_descriptor[str_end - str_start] = '\0';
    } else {
        dpu_pr_err("[EDID] The length of monitor name is wrong");
        return -EINVAL;
    }

    return 0;
}

static int get_block_type(const uint8_t *block)
{
    if ((block[0] == 0) && (block[1] == 0)) {
        /* Other descriptor */
        if ((block[2] != 0) || (block[4] != 0))
            return UNKNOWN_DESCRIPTOR;
        return block[3];
    }
    /* Detailed timing block */
    return DETAILED_TIMING_BLOCK;
}

static void parse_video_input_definition(const uint8_t definition, struct edid_video_base_info *video_base_info)
{
    if (definition & 0x80) {
        dpu_pr_info("[EDID] Input is a Digital Video Signal Interface");

        if ((definition & 0x70) == 0x00) {
            dpu_pr_warn("[EDID] Color depth is undefined");
        } else if ((definition & 0x70) == 0x70) {
            dpu_pr_warn("[EDID] Color Bit Depth set to reserved value");
        } else {
            video_base_info->bits_per_color = ((definition & 0x70) >> 3) + 4;
            dpu_pr_info("[EDID] Bits per primary color channel: %u", video_base_info->bits_per_color);
        }

        switch (definition & 0x0F) {
            case 1:
                video_base_info->dvi_input = 1;
                dpu_pr_info("[EDID] DVI is supported.");
                break;
            case 2:
                video_base_info->hdmia_input = 1;
                dpu_pr_info("[EDID] HDMI-a is supported.");
                break;
            case 3:
                video_base_info->hdmib_input = 1;
                dpu_pr_info("[EDID] HDMI-b is supported.");
                break;
            case 4:
                video_base_info->mddi_input = 1;
                dpu_pr_info("[EDID] MDDI is supported.");
                break;
            case 5:
                video_base_info->dp_input = 1;
                dpu_pr_info("[EDID] DisplayPort is supported.");
                break;
            default:
                dpu_pr_info("[EDID] Reserved: Do Not Use");
                break;
        }
    } else {
        dpu_pr_info("[EDID] Input is an Analog Video Signal Interface");
    }
}

static int dtd_timing_info_add_list(struct edid_video_timing *vid_info, const struct dtd *edid_timing, bool is_native, bool est_timing)
{
    struct edid_timing_node *node = NULL;

    node = kzalloc(sizeof(struct edid_timing_node), GFP_KERNEL);
    if (node != NULL) {
        node->timing_info.hactive_pixels = edid_timing->h_active;
        node->timing_info.hblanking = edid_timing->h_blanking;
        node->timing_info.hsync_offset = edid_timing->h_sync_offset;
        node->timing_info.hsync_pulse_width = edid_timing->h_sync_pulse_width;
        node->timing_info.hsize = edid_timing->h_image_size;

        node->timing_info.vactive_pixels = edid_timing->v_active;
        node->timing_info.vblanking = edid_timing->v_blanking;
        node->timing_info.vsync_offset = edid_timing->v_sync_offset;
        node->timing_info.vsync_pulse_width = edid_timing->v_sync_pulse_width;
        node->timing_info.vsize = edid_timing->v_image_size;

        node->timing_info.pixel_clock = edid_timing->pixel_clock; // uint:KHz
        node->timing_info.interlaced = edid_timing->interlaced;
        node->timing_info.hsync_polarity = edid_timing->h_sync_polarity;
        node->timing_info.vsync_polarity = edid_timing->v_sync_polarity;
		node->timing_info.preferred = false;

        if (edid_calculate_fps(&node->timing_info, &node->timing_info.fps) != 0) {
            kfree(node);
            return -EINVAL;
        }

        vid_info->main_vcount += 1;
        list_add_tail(&node->list_node, &vid_info->video_timing_list);
        if (!est_timing)
            parse_priority_timing_description(vid_info, node, is_native);
    } else {
        dpu_pr_err("[EDID] kzalloc struct hdmi_vic fail!");
        return -EINVAL;
    }

    dpu_pr_info("[EDID] The timinginfo %hhu: hactive_pixels: %hu hblanking: %hu hsync_offset: %hu \
        hsync_pulse_width: %hu hsize: %hu", vid_info->main_vcount, node->timing_info.hactive_pixels, node->timing_info.hblanking,
        node->timing_info.hsync_offset, node->timing_info.hsync_pulse_width, node->timing_info.hsize);
    dpu_pr_info("[EDID] The timinginfo %hhu: vactive_pixels: %hu vblanking: %hu vsync_offset: %hu \
        vsync_pulse_width: %hu vsize: %hu", vid_info->main_vcount, node->timing_info.vactive_pixels, node->timing_info.vblanking,
        node->timing_info.vsync_offset, node->timing_info.vsync_pulse_width, node->timing_info.vsize);

    dpu_pr_info("[EDID] The timinginfo %hhu: hactive_pixels: %hu vactive_pixels: %hu fps: %u pixel clock: %llu",
        vid_info->main_vcount, node->timing_info.hactive_pixels, node->timing_info.vactive_pixels,
		node->timing_info.fps, node->timing_info.pixel_clock);
    return 0;
}

static int parse_establish_timing_description(struct edid_video_timing *vid_info, const struct dtd *edid_timing)
{
    int ret = 0;

    /* Get Max Value by comparing all values */
    if ((vid_info->max_hpixels < edid_timing->h_active) && (vid_info->max_vpixels <= edid_timing->v_active) &&
        (vid_info->max_pixel_clock < edid_timing->pixel_clock)) {
        vid_info->max_hpixels = edid_timing->h_active;
        vid_info->max_vpixels = edid_timing->v_active;
        vid_info->max_pixel_clock = edid_timing->pixel_clock;
        dpu_pr_info("[EDID] max_pixel_clock 2 is %llu", vid_info->max_pixel_clock);
    }

    ret = dtd_timing_info_add_list(vid_info, edid_timing, false, true);
    if (ret != 0) {
        dpu_pr_err("[EDID] add timing information to video_timing_list fail !");
        return -EINVAL;
    }

    return 0;
}

static int parse_established_timing(const uint8_t *edid, struct edid_video_timing *video_timing)
{
    int i = 0;
    int count = 0;
    int ret = 0;
    int array_index = 0;
    int bit_index = 0;

    for (i = 0; i < ESTABLISHED_TIMINGS_BIT_SIZE; i++) {
        array_index = i / 8;
        bit_index = (i == (ESTABLISHED_TIMINGS_BIT_SIZE - 1)) ? 7 : (i % 8);
        if (edid[ESTABLISHED_TIMINGS_START + array_index] & ESTABLISHED_TIMINGS(bit_index)) {
            ret = parse_establish_timing_description(video_timing, &edid_estab_timing[i]);
            if (ret != 0) {
                dpu_pr_warn("[EDID] fail parse establish_timing.");
                continue;
            }
            count++;
        }
    }

    return count;
}

static bool is_valid_standard_timing(uint8_t hdisplay_size, uint8_t freq_aspect)
{
	if ((hdisplay_size == 0x00 && freq_aspect == 0x00) ||
		(hdisplay_size == 0x01 && freq_aspect == 0x01))
		return false;

	return true;
}

static uint8_t get_standard_timing_mode(struct edid_video_timing *video_timing, const uint8_t *edid)
{
	video_timing->edid_feature_support = (edid[EDID_FEATURE_SUPPORT] & EDID_FEATURE_SUPPORT_TIMING_MODE);

	/*
	With EDID Structure version 1, revision 4, GTF has been Deprecated, (GTF is considered obsolete
	and in the process of being phased out) in favor of CVT. GTF has been retained in EDID Structure
	version 1, revision 4 for legacy support only and may be retired in a future release of the E-EDID
	Standard. VESA no longer recommends using GTF. However, GTF is probably still supported in some
	EDID v1.4 of manufacture's monitor, so GTF and GTF2 are also supported here in EDID v1.4. The priciple
	here that EDID v1.4 or higher version only support CVT may be applicated in the future.
	*/
	if (edid[EDID_STRUCT_VERSION] == 1 && edid[EDID_STRUCT_REVISION] >= 3 && video_timing->edid_feature_support != 0) {
		if (video_timing->video_timing_support_flag == DEFAULT_GTF_SUPPORTED)
			return VESA_GTF_MODE;
		if (video_timing->video_timing_support_flag == SECONDARY_GTF_SUPPORTED)
			return VESA_GTF2_MODE;
		if (edid[EDID_STRUCT_REVISION] >= 4)
			return VESA_CVT_MODE;
		if (edid[EDID_STRUCT_REVISION] == 3)
			return VESA_GTF_MODE;
	}

	return VESA_DMT_MODE;
}

static bool get_dmt_mode_timing(struct dtd *edid_timing, uint16_t fps)
{
	int i;
	uint16_t dmt_fps;
	uint64_t pixels;

    for (i = 1; i < ARRAY_SIZE(dmt_modes_dtd); i++) {
		if (dmt_modes_dtd[i].interlaced != PROGRESSIVE)
			continue;
        if (edid_timing->h_active != dmt_modes_dtd[i].h_active)
			continue;
        if (edid_timing->v_active != dmt_modes_dtd[i].v_active)
            continue;

		pixels = (uint64_t)((dmt_modes_dtd[i].h_active + dmt_modes_dtd[i].h_blanking) *
			(dmt_modes_dtd[i].v_active + dmt_modes_dtd[i].v_blanking));
		dmt_fps = (uint16_t)(dmt_modes_dtd[i].pixel_clock * 1000 / pixels);
		dpu_pr_debug("[EDID] index:%d, cal_fps:%u, pixel_clock:%llu", i, dmt_fps, dmt_modes_dtd[i].pixel_clock);
		if ((dmt_fps < fps - 1) || (dmt_fps > fps))
            continue;

		edid_timing->h_active = dmt_modes_dtd[i].h_active;
        edid_timing->h_blanking = dmt_modes_dtd[i].h_blanking;
		edid_timing->h_sync_offset = dmt_modes_dtd[i].h_sync_offset;
		edid_timing->h_sync_pulse_width = dmt_modes_dtd[i].h_sync_pulse_width;
		edid_timing->h_sync_polarity = dmt_modes_dtd[i].h_sync_polarity;
		edid_timing->h_image_size = dmt_modes_dtd[i].h_image_size;

		edid_timing->v_active = dmt_modes_dtd[i].v_active;
        edid_timing->v_blanking = dmt_modes_dtd[i].v_blanking;
        edid_timing->v_sync_offset = dmt_modes_dtd[i].v_sync_offset;
		edid_timing->v_sync_pulse_width = dmt_modes_dtd[i].v_sync_pulse_width;
        edid_timing->v_sync_polarity = dmt_modes_dtd[i].v_sync_polarity;
		edid_timing->v_image_size = dmt_modes_dtd[i].v_image_size;

		edid_timing->interlaced = dmt_modes_dtd[i].interlaced;
		edid_timing->pixel_clock = dmt_modes_dtd[i].pixel_clock;

		dpu_pr_info("[EDID] Get dmt mode[%d][%u * %u @ %uHz]", i, edid_timing->h_active, edid_timing->v_active, dmt_fps);
		return true;
	}

	return false;
}

static void get_cvt_rbv2_timing(struct dtd *edid_timing, uint16_t fps)
{
	uint16_t h_blank_addition = 200;
	uint32_t h_period = 0;
	uint64_t pixels = 0;

	/* estimate times per line(horizontal period us) */
	h_period = (uint32_t)((1000000 - CVT_RBV2_MIN_V_BLANK * fps) / (edid_timing->v_active * fps));
	/* find vertical blanking lines */
	edid_timing->v_blanking = (uint16_t)(CVT_RBV2_MIN_V_BLANK / h_period + 1);
	if (edid_timing->v_blanking < (CVT_RBV2_V_SYNC + CVT_RBV2_MIN_V_FRONT_PORCH + CVT_RBV2_MIN_V_BACK_PORCH))
		edid_timing->v_blanking = CVT_RBV2_V_SYNC + CVT_RBV2_MIN_V_FRONT_PORCH + CVT_RBV2_MIN_V_BACK_PORCH;
	edid_timing->v_sync_offset = CVT_RBV2_MIN_V_FRONT_PORCH;
	edid_timing->v_sync_pulse_width = CVT_RBV2_V_SYNC;
	edid_timing->v_sync_polarity = CVT_RBV2_V_POLARITY;
	edid_timing->h_blanking = CVT_RBV2_MIN_H_BLANK + h_blank_addition;
	edid_timing->h_sync_offset = CVT_RBV2_H_FRONT_PORCH;
	edid_timing->h_sync_pulse_width = CVT_RBV2_H_SYNC;
	edid_timing->h_sync_polarity = CVT_RBV2_H_POLARITY;
	dpu_pr_info("[EDID] Get cvt mode hblank is %u, vblank is %u", edid_timing->v_blanking, edid_timing->h_blanking);
	pixels = (uint64_t)((edid_timing->h_active + edid_timing->h_blanking) *
			(edid_timing->v_active + edid_timing->v_blanking));
	edid_timing->pixel_clock = (uint64_t)fps * pixels / 1000;
	/* non-interlaced scan */
	edid_timing->interlaced = PROGRESSIVE;
	dpu_pr_info("[EDID] Get cvt mode[%u * %u @ %uHz]", edid_timing->h_active, edid_timing->v_active, fps);
}

static bool get_cvt_mode_timing(struct dtd *edid_timing, uint16_t fps, enum cvt_version cvt)
{
	bool ret = false;
	/*
	Standard CRT-based Timing and CVT Reduced Blanking Timing Version 1 are removed from CVT2.1,
	if cvt_rbv 1 is required, please refer to CVT1.2 standard and add corresponding formula to 
	this function. cvt_rbv 3 is used for adptive-sync operation, if cvt_rbv 3 is required, please
	refer to CVT2.1 standard and add corresponding formula to this function.
	*/
	switch (cvt) {
		case CVT_CRT:
			dpu_pr_info("[EDID] CVT_CRT version is not required yet");
			break;
		case CVT_RBV1:
			dpu_pr_info("[EDID] CVT_RBV1 version is not required yet");
			break;
		case CVT_RBV2:
			get_cvt_rbv2_timing(edid_timing, fps);
			ret = true;
			break;
		case CVT_RBV3:
			dpu_pr_info("[EDID] CVT_RBV3 version is not required yet");
			break;
        default:
			dpu_pr_warn("[EDID] Invalid CVT mode");
			return false;
	}

	return ret;
}

static bool get_gtf_mode_timing(struct edid_video_timing *video_timing, struct dtd *edid_timing, uint16_t fps, enum gtf_version gtf)
{
	uint16_t gtf_m;
    uint16_t gtf_c;
    uint16_t gtf_k;
	uint16_t gtf_j;
	uint16_t gtf_c_prime;
	uint16_t gtf_m_prime;
	uint16_t total_hactive = 0;
	uint16_t total_hpixels = 0;
	uint16_t vback_porch = 0;
	uint16_t ideal_hb_duty_cycle = 0;
	uint32_t h_period = 0;
	uint64_t pixels = 0;

	if (gtf == GTF_DEFAULT) {
		gtf_m = GTF_DEFAULT_M;
		gtf_c = GTF_DEFAULT_C;
		gtf_k = GTF_DEFAULT_K;
        gtf_j = GTF_DEFAULT_J;
		edid_timing->h_sync_polarity = NEGATIVE;
		edid_timing->v_sync_polarity = POSITIVE;
    } else if (gtf == GTF_SECONDARY) {
		gtf_m = video_timing->gtf2_m;
		gtf_c = video_timing->gtf2_c / 2;
		gtf_k = video_timing->gtf2_k;
		gtf_j = video_timing->gtf2_j / 2;
		edid_timing->h_sync_polarity = POSITIVE;
		edid_timing->v_sync_polarity = NEGATIVE;
	} else {
		dpu_pr_warn("[EDID] Invalid GTF mode");
		return false;
	}

	/* estimate times per line(horizontal period us) */
	h_period = (uint32_t)(1000000 - GTF_MIN_V_SYNC_PLUS_BP * fps) / ((edid_timing->v_active + GTF_MIN_V_FRONT_PORCH) * fps);
	/* find the number of lines of the vertical back porch */
	vback_porch = (uint16_t)(GTF_MIN_V_SYNC_PLUS_BP / h_period - GTF_V_SYNC);
	/* get vertical blanking parameters */
	edid_timing->v_blanking = GTF_MIN_V_FRONT_PORCH + GTF_V_SYNC + vback_porch;
	edid_timing->v_sync_offset = GTF_MIN_V_FRONT_PORCH;
	edid_timing->v_sync_pulse_width = GTF_V_SYNC;
	dpu_pr_info("[EDID] v_blanking is %u, v_sync_offset is %u, v_sync_pulse_width is %u", edid_timing->v_blanking,
		edid_timing->v_sync_offset, edid_timing->v_sync_pulse_width);
	/*
	get the horizontal blanking duty cycle(%) which indicates the percentage of the horizontal blanking per line
	according to the Generalized Timing Formula(GTF) standard.
	*/
	gtf_c_prime = (gtf_c - gtf_j) * gtf_k / 256 + gtf_j;
	gtf_m_prime = gtf_k * gtf_m / 256;
	ideal_hb_duty_cycle = (uint16_t)(gtf_c_prime - gtf_m_prime * h_period / 1000);
	dpu_pr_info("[EDID] [GTF_M*GTF_C*GTF_K*GTF_J]=[%u,%u,%u,%u], [GTF_M_PRIME*GTF_C_PRIME]=[%u,%u], ideal_hb_duty_cycle=%u",
		gtf_m, gtf_c, gtf_k, gtf_j, gtf_m_prime, gtf_c_prime, ideal_hb_duty_cycle);
	/*
	The number of horizontal pixels requested is first processed to ensure that it is divisible by the character size,
	by rounding it to the nearest character cell boundary. The assumed character cell granularity are 8 pixels.
	*/
	total_hactive = (edid_timing->h_active + GTF_CELL_GRAN / 2) / GTF_CELL_GRAN;
	total_hactive = total_hactive * GTF_CELL_GRAN;
	/* get the horizontal blanking(in pixels) */
	edid_timing->h_blanking = total_hactive * ideal_hb_duty_cycle / (100 - ideal_hb_duty_cycle);
	edid_timing->h_blanking = (edid_timing->h_blanking + GTF_CELL_GRAN) / (2 * GTF_CELL_GRAN);
	edid_timing->h_blanking = edid_timing->h_blanking * 2 * GTF_CELL_GRAN;
	/* get total number of the horizontal pixels */
	total_hpixels = total_hactive + edid_timing->h_blanking;
	/* get the horizontal sync width(in pixels) */
	edid_timing->h_sync_pulse_width = total_hpixels * GTF_H_SYNC_PERCENT / 100;
	edid_timing->h_sync_pulse_width = (edid_timing->h_sync_pulse_width + GTF_CELL_GRAN / 2) / GTF_CELL_GRAN;
	edid_timing->h_sync_pulse_width = edid_timing->h_sync_pulse_width * GTF_CELL_GRAN;
	/* get the horizontal front porch(in pixels) */
	edid_timing->h_sync_offset = edid_timing->h_blanking / 2 - edid_timing->h_sync_pulse_width;
	dpu_pr_info("[EDID] h_blanking is %u, h_sync_offset is %u, h_sync_pulse_width is %u",
		edid_timing->h_blanking, edid_timing->h_sync_offset, edid_timing->h_sync_pulse_width);
	/* get the pixel clock */
	pixels = (uint64_t)((edid_timing->h_active + edid_timing->h_blanking) *
			(edid_timing->v_active + edid_timing->v_blanking));
	edid_timing->pixel_clock = (uint64_t)fps * pixels / 1000;
	/* non-interlaced scan */
	edid_timing->interlaced = PROGRESSIVE;
	dpu_pr_info("[EDID] Get gtf mode[%u * %u @ %uHz]", edid_timing->h_active, edid_timing->v_active, fps);

	return true;
}

static int parse_standard_timing_description(struct edid_video_timing *video_timing, struct dtd *edid_timing, uint16_t fps, uint8_t mode)
{
	int ret = 0;
	dpu_pr_info("[EDID] parse_standard_timing_description start");

	if (get_dmt_mode_timing(edid_timing, fps))
		return ret;

	switch (mode) {
		case VESA_DMT_MODE:
		case VESA_CVT_MODE:
			if (!get_cvt_mode_timing(edid_timing, fps, CVT_RBV2))
				ret = -EINVAL;
			break;
		case VESA_GTF_MODE:
			if (!get_gtf_mode_timing(video_timing, edid_timing, fps, GTF_DEFAULT))
				ret = -EINVAL;
            break;
		case VESA_GTF2_MODE:
            if (!get_gtf_mode_timing(video_timing, edid_timing, fps, GTF_SECONDARY))
				ret = -EINVAL;
            break;
		default:
			dpu_pr_warn("[EDID] Invalid timing mode");
			return -EINVAL;
    }

	return ret;
}

static int parse_standard_timing(const uint8_t *edid, struct edid_video_timing *video_timing)
{
	int ret = 0;
	int i = 0;
	int count = 0;
	uint8_t t_mode = 0;
	uint8_t hdisplay_size = 0;
	uint8_t freq_aspect = 0;
	uint16_t h_active = 0;
	uint16_t image_fps = 0;
	struct dtd standard_timing = {0};

	t_mode = get_standard_timing_mode(video_timing, edid);
	dpu_pr_info("[EDID] First get standard timing mode is %u", t_mode);

	for (i = 0; i <= 14; i += 2) {
		hdisplay_size = edid[STANDARD_TIMINGS_START + i];
		freq_aspect = edid[STANDARD_TIMINGS_START + i + 1];
		if (is_valid_standard_timing(hdisplay_size, freq_aspect)) {
			if (IMAGE_ASPECT_RATIO_INDEX(freq_aspect) == 0 && edid[EDID_STRUCT_REVISION] < 3) {
				standard_timing.h_active = standard_timing.v_active = (hdisplay_size + 31) * 8;
				standard_timing.h_image_size = standard_timing.v_image_size = 1;
			} else {
				standard_timing.h_active = h_active = (uint16_t)(hdisplay_size + 31) * 8;
				standard_timing.v_active = h_active * IMAGE_V_RATIO(freq_aspect) / IMAGE_H_RATIO(freq_aspect);
				standard_timing.h_image_size = (uint16_t)IMAGE_H_RATIO(freq_aspect);
				standard_timing.v_image_size = (uint16_t)IMAGE_V_RATIO(freq_aspect);
			}
			image_fps = (uint16_t)(EDID_BASE_FPS + REFRESH_RATE(freq_aspect));
			/* The range of fps in EDID standard timing is from 60Hz to 123Hz */
			if (image_fps > 123) {
				dpu_pr_err("[EDID] invalid fps, igonre this timing");
				continue;
			}
			ret = parse_standard_timing_description(video_timing, &standard_timing, image_fps, t_mode);
			if (ret == 0) {
				dtd_timing_info_add_list(video_timing, &standard_timing, false, true);
				count++;
			}
		}
	}

	return count;
}

static void parse_edid_product_info(const uint8_t *edid, const uint32_t edid_len,
    struct edid_product_info *out_edid_product_info)
{
    /* Check EDID Manufacturer Name */
    char manu_name[VENDOR_NAME_LEN] = {0};

    manu_name[0] = (char)((((edid[EDID_FACTORY_START] << 8) | edid[EDID_FACTORY_START + 1]) & 0x7C00) >> 10) + 'A' - 1;
    manu_name[1] = (char)((((edid[EDID_FACTORY_START] << 8) | edid[EDID_FACTORY_START + 1]) & 0x03E0) >> 5) + 'A' - 1;
    manu_name[2] = (char)(((edid[EDID_FACTORY_START] << 8) | edid[EDID_FACTORY_START + 1]) & 0x001F) + 'A' - 1;
    manu_name[3] = '\0';
    if (sprintf_s(out_edid_product_info->vendor_name, VENDOR_NAME_LEN, "%s", manu_name) <= 0) {
        dpu_pr_warn("[EDID] create vendor name failed");
    }

    /* Check EDID Product Code */
    out_edid_product_info->product_code = (uint32_t)(edid[EDID_FACTORY_START + 2] | (edid[EDID_FACTORY_START + 3] << 8));

    /* Check EDID Serial Number */
    out_edid_product_info->serial_number = edid[EDID_FACTORY_START + 4] | (edid[EDID_FACTORY_START + 5] << 8) |
        (edid[EDID_FACTORY_START + 6] << 16) | (edid[EDID_FACTORY_START + 7] << 24) ;

    /* Check EDID Week & Year of Manufacture or Model Year */
    out_edid_product_info->manufacture_week = (int)edid[EDID_FACTORY_START + 8];
    out_edid_product_info->manufacture_year = (int)edid[EDID_FACTORY_START + 9] + 1990;

    /* Check EDID version */
    dpu_pr_info("[EDID] EDID version %d revision %d", (int)edid[EDID_STRUCT_VERSION],
        (int)edid[EDID_STRUCT_REVISION]);
}

static int parse_main(const uint8_t *edid, struct edid_video *out_edid_video_info)
{
    int16_t i = 0;
    int ret = 0;
    const uint8_t *block = NULL;
    struct edid_video_base_info *video_base_info = &out_edid_video_info->video_base_info;
    struct edid_video_timing *video_timing = &out_edid_video_info->video_timing;

    parse_video_input_definition(edid[EDID_INPUT_DEFINITION], video_base_info);

    /* Check Display Image Size(Physical) */
    video_timing->max_himage_size = (uint16_t)edid[HORIZONTAL_SCREEN_SIZE];
    video_timing->max_vimage_size = (uint16_t)edid[VERTICAL_SCREEN_SIZE];
    dpu_pr_info("[EDID] parse Screen Size: max_himage_size: %hu max_vimage_size: %hu",
        video_timing->max_himage_size, video_timing->max_vimage_size);

    /* Parse the EDID Detailed Timing Descriptor */
    block = edid + DETAILED_TIMING_DESCRIPTIONS_START;
    /* EDID main part has a total of four Descriptor Block */
    for (i = 0; i < DETAILED_TIMING_DESCRIPTION_COUNT; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
        dpu_pr_info("[EDID] Detailed_timing_descriptions blockid: %hd", i);
        switch (get_block_type(block)) {
        case DETAILED_TIMING_BLOCK:
            ret = parse_video_timing_description(video_timing, block, edid);
            if (ret != 0) {
                dpu_pr_err("[EDID] Timing Description Parsing failed!");
            }
            break;
        case MONITOR_LIMITS:
            parse_monitor_video_limits(video_timing, block);
            break;
        case MONITOR_NAME:
            ret = parse_monitor_name(video_timing, block, DETAILED_TIMING_DESCRIPTION_SIZE);
            if (ret != 0) {
                dpu_pr_err("[EDID] The monitor name parsing failed.");
                return ret;
            }
            break;
        default:
            break;
        }
    }

    dpu_pr_info("[EDID] Extensions to follow:\t%d", (int)edid[EXTENSION_FLAG]);
    /* Return the number of following extension blocks */
    return (int16_t)edid[EXTENSION_FLAG];
}

static int check_exten_edid(const uint8_t *exten)
{
    uint32_t i = 0;
    uint32_t checksum = 0;

    for (i = 0; i < EDID_LENGTH; i++)
        checksum += exten[i];
    if (checksum & 0xFF) {
        dpu_pr_err("[EDID] Extension Data checksum failed - data is corrupt. Continuing anyway.");
        return -EINVAL;
    }
    /* Check Extension Tag */
    if (exten[0] != EXTENSION_HEADER_CEA && exten[0] != EXTENSION_HEADER_DISPLAYID) {
        dpu_pr_warn("[EDID] Not CEA-EDID Timing Extension, Extension-Parsing will not continue!");
        return -EINVAL;
    }
    return 0;
}

static int check_exten_edid_displayid(const uint8_t *exten)
{
    uint16_t dtd_total = 0;
    uint32_t checksum = 0;
    uint32_t i = 0;
    uint32_t version = 0;
    uint32_t revsersion = 0;
    uint32_t product_type = 0;
    uint32_t ext_count = 0;

    version = (uint32_t)((exten[1] & 0xF0) >> 4);
    revsersion = (uint32_t)(exten[1] & 0x0F);
    dtd_total = exten[2];
    product_type = exten[3];
    ext_count = exten[4];

    dpu_pr_info("[EDID] exten_edid_displayid head : version = %u; revsersion = %u; dtd_total = %u; \
        product_type = %u; ext_count = %u.", version, revsersion, dtd_total, product_type, ext_count);

    if (exten[1] != 0x20) {
        dpu_pr_warn("[EDID] EDID is not V2.0");
        return -EINVAL;
    } else if (exten[3] != 0x02) {
        dpu_pr_warn("[EDID] EDID is not Generic display");
        return -EINVAL;
    }

    if (dtd_total != EDID_LENGTH - 2 - 5) {
        dpu_pr_err("[EDID] dtd_total err, dtd_total: %hu", dtd_total);
        return -EINVAL;
    }

    for (i = 5; i < EDID_LENGTH - 1; i++)
        checksum += exten[i];
    if (checksum & 0xFF)
        dpu_pr_err("[EDID] dtd_total: %hu, checksum: %u", dtd_total, checksum);

    return 0;
}

static int parse_video_timing_description_displayid(struct edid_video_timing *vid_info, const uint8_t *dtd)
{
    struct edid_timing_node *node = NULL;

    /* Get Max Value by comparing all values */
    if ((vid_info->max_hpixels < H_ACTIVE_DISPLAYID(dtd)) && (vid_info->max_vpixels <= V_ACTIVE_DISPLAYID(dtd)) &&
        (vid_info->max_pixel_clock < PIXEL_CLOCK_DISPLAYID(dtd))) {
        vid_info->max_hpixels = H_ACTIVE_DISPLAYID(dtd);
        vid_info->max_vpixels = V_ACTIVE_DISPLAYID(dtd);
        vid_info->max_pixel_clock = PIXEL_CLOCK_DISPLAYID(dtd);
        dpu_pr_info("[EDID] max_pixel_clock 2 is %llu", vid_info->max_pixel_clock);
    }

    node = kzalloc(sizeof(struct edid_timing_node), GFP_KERNEL);
    if (node != NULL) {
        node->timing_info.hactive_pixels = H_ACTIVE_DISPLAYID(dtd);
        node->timing_info.hblanking = H_BLANKING_DISPLAYID(dtd);
        node->timing_info.hsync_offset = H_SYNC_OFFSET_DISPLAYID(dtd);
        node->timing_info.hsync_pulse_width = H_SYNC_WIDTH_DISPLAYID(dtd);

        node->timing_info.vactive_pixels = V_ACTIVE_DISPLAYID(dtd);
        node->timing_info.vblanking = V_BLANKING_DISPLAYID(dtd);
        node->timing_info.vsync_offset = V_SYNC_OFFSET_DISPLAYID(dtd);
        node->timing_info.vsync_pulse_width = V_SYNC_WIDTH_DISPLAYID(dtd);

        node->timing_info.pixel_clock = PIXEL_CLOCK_DISPLAYID(dtd); // uint:KHz
		node->timing_info.preferred = false;

        if (edid_calculate_fps(&node->timing_info, &node->timing_info.fps) != 0) {
            kfree(node);
            return -EINVAL;
        }

        vid_info->main_vcount += 1;
        list_add_tail(&node->list_node, &vid_info->video_timing_list);
        parse_priority_timing_description(vid_info, node, false);
    } else {
        dpu_pr_err("[EDID] kzalloc struct hdmi_vic fail!");
        return -EINVAL;
    }

    dpu_pr_info("main_vcount: %hhu hactive_pixels: %hu hblanking: %hu hsync_offset: %hu hsync_pulse_width: %hu \
        hborder: %hu hsize: %hu", vid_info->main_vcount, node->timing_info.hactive_pixels, node->timing_info.hblanking,
        node->timing_info.hsync_offset, node->timing_info.hsync_pulse_width, node->timing_info.hborder, node->timing_info.hsize);
    dpu_pr_info("vactive_pixels: %hu vblanking: %hu vsync_offset: %hu vsync_pulse_width: %hu \
        vborder: %hu vsize: %hu", node->timing_info.vactive_pixels, node->timing_info.vblanking, node->timing_info.vsync_offset,
        node->timing_info.vsync_pulse_width, node->timing_info.vborder, node->timing_info.vsize);

    dpu_pr_info("[EDID] The timinginfo %hhu: hactive_pixels: %hu vactive_pixels: %hu \
        fps: %hu pixel clock: %llu", vid_info->main_vcount, node->timing_info.hactive_pixels,
        node->timing_info.vactive_pixels, node->timing_info.fps, node->timing_info.pixel_clock);

    return 0;
}

static int parse_video_displayid_extension(struct edid_video_timing *video_timing, const uint8_t *exten)
{
    int ret = 0;
    uint8_t dts_data_block_position = 0;
    uint16_t dtd_total = 0;

    dpu_pr_info("[EDID] parse_video_displayid_extension +");

    ret = check_exten_edid(exten);
    if (ret) {
        dpu_pr_warn("[EDID] The check_exten_edid failed");
        return ret;
    }
    /*
     * Get uint8_t number (decimal) within this block where the 18-uint8_t DTDs begin.
     * ( Number data stored in the third uint8_t )
     */
    ret = check_exten_edid_displayid(exten);
    if (ret) {
        dpu_pr_warn("[EDID] The check_exten_edid_displayid failed");
        return ret;
    }

    dts_data_block_position = 5;
    dtd_total = exten[2];

    while (dts_data_block_position < dtd_total + 5 - 2 && exten[dts_data_block_position] != 0) {
        switch (exten[dts_data_block_position]) {
            case 0x22:
                dpu_pr_info("[EDID] parse start 0x%x", dts_data_block_position);
                parse_video_timing_description_displayid(video_timing, exten + dts_data_block_position);
                break;
            default:
                dpu_pr_warn("[EDID] jump of 0x%x", exten[dts_data_block_position]);
                break;
        }
        if (exten[dts_data_block_position + 2] == 0x0) {
            dpu_pr_info("[EDID] dts_data_block_position is end, P: %hhu", dts_data_block_position);
            break;
        }
        dpu_pr_info("[EDID] continue dts_data_block_position: %hhu, size: 0x%x", dts_data_block_position,
            exten[dts_data_block_position + 2]);
        dts_data_block_position = dts_data_block_position + exten[dts_data_block_position + 2] + 3;
    }
    dpu_pr_info("[EDID] parse_video_displayid_extension -");
    return 0;
}

static int parse_audio_spec_info(struct edid_audio *aud_info, struct edid_audio_info *spec_info,
    const uint8_t *cea_data_block)
{
    if ((EXTEN_AUDIO_FORMAT(cea_data_block) <= 8) && (EXTEN_AUDIO_FORMAT(cea_data_block) >= 1)) {
        dpu_check_and_return((!aud_info || !spec_info), -EINVAL, err, "[EDID] aud_info or spec_info is NULL!");
        /* Set up SAD fields */
        spec_info->format = EXTEN_AUDIO_FORMAT(cea_data_block);
        spec_info->channels =  EXTEN_AUDIO_MAX_CHANNELS(cea_data_block);
        spec_info->sampling = EXTEN_AUDIO_SAMPLING(cea_data_block);
        if (EXTEN_AUDIO_FORMAT(cea_data_block) == 1)
            spec_info->bitrate = EXTEN_AUDIO_LPCM_BIT(cea_data_block);
        else
            spec_info->bitrate = EXTEN_AUDIO_BITRATE(cea_data_block);
        aud_info->ext_acount += 1;

        dpu_pr_info("[EDID] parse audio spec success. format(0x%x), channels(0x%x), sampling(0x%x), bitrate(0x%x)",
            spec_info->format, spec_info->channels, spec_info->sampling, spec_info->bitrate);
    } else {
        dpu_pr_info("[EDID] ignore the audio_spec info.");
    }

    return 0;
}

static int parse_extension_audio_tag(struct edid_audio *aud_info, const uint8_t *cea_data_block, uint8_t temp_length)
{
    uint8_t i = 0;
    struct edid_audio_info *edid_audio_info = NULL;

    for (i = 0; i < (temp_length / 3); i++) {
        edid_audio_info = kzalloc(sizeof(struct edid_audio_info), GFP_KERNEL);
        if (edid_audio_info == NULL) {
            dpu_pr_err("[EDID] malloc edid_audio_info failed!");
            return -EINVAL;
        }

        if (parse_audio_spec_info(aud_info, edid_audio_info, cea_data_block)) {
            dpu_pr_err("[EDID] parse the audio spec info fail.");
            kfree(edid_audio_info);
            return -EINVAL;
        }
        list_add_tail(&edid_audio_info->list_node, &aud_info->spec);
        cea_data_block += 3;
    }
    return 0;
}

static void vary_vfp(struct dtd *dtd_by_code, uint8_t code, uint32_t refresh_rate)
{
    uint8_t i = 0;
    uint8_t j = 0;

    for (i = 0; i < (uint8_t)ARRAY_SIZE(hm_vary_vfp_tlb); i++) {
        if (code == hm_vary_vfp_tlb[i].code) {
            /* valid_count must be 1 or 2, so < ARRAYSIZE(hm_vary_vfp_tlb[i].data) */
            for (j = 0; j < hm_vary_vfp_tlb[i].valid_count; j++) {
                if (refresh_rate == hm_vary_vfp_tlb[i].data[j].refresh_rate) {
                    dtd_by_code->v_blanking = hm_vary_vfp_tlb[i].data[j].v_blanking;
                    dtd_by_code->v_sync_offset = hm_vary_vfp_tlb[i].data[j].v_sync_offset;
                    return;
                }
            }
            if (j > 0) {
                dtd_by_code->v_blanking = hm_vary_vfp_tlb[i].data[--j].v_blanking + 1;
                dtd_by_code->v_sync_offset = hm_vary_vfp_tlb[i].data[--j].v_sync_offset + 1;
            }
        }
    }
}

static bool convert_code_to_dtd(struct dtd *mdtd, uint8_t code, uint32_t refresh_rate, uint8_t video_format)
{
    uint8_t code_invalid_start_index = 0;
    uint8_t cta_part2_start_index = 193;
    struct dtd_info support_dtd_info[] = {
        { .size = (uint8_t)dtd_array_size(cta_modes_dtd_part1), .support_modes_dtd = cta_modes_dtd_part1 },
        { .size = (uint8_t)dtd_array_size(cta_modes_dtd_part2), .support_modes_dtd = cta_modes_dtd_part2 },
        { .size = (uint8_t)dtd_array_size(dmt_modes_dtd), .support_modes_dtd = dmt_modes_dtd }
    };
    struct dtd_info *handle_dtd = NULL;

    if (memset_s(mdtd, sizeof(*mdtd), 0, sizeof(*mdtd)) != EOK) {
        dpu_pr_err("[EDID] memset mdtd failed");
        return false;
    }

    if (video_format > DMT || code == code_invalid_start_index) {
        dpu_pr_warn("[EDID] video_format=%hhu or code=%hhu is error", video_format, code);
        return false;
    }

    handle_dtd = &support_dtd_info[video_format];
    if (video_format == CTA && code >= cta_part2_start_index) {
        code = code - cta_part2_start_index;
        handle_dtd = &support_dtd_info[video_format + 1];
    }

    if (code >= handle_dtd->size) {
        dpu_pr_warn("[EDID] code is %hhu which exceeds the upper limit", code);
        return false;
    }

    if (handle_dtd->support_modes_dtd[code].interlaced == INTERLACED) {
        dpu_pr_info("[EDID] Interlaced scan mode is not supported, code is %hhu", code);
        return false;
    }

    *mdtd = handle_dtd->support_modes_dtd[code];
    dpu_pr_info("[EDID] convert_code_to_dtd, pixel_clock is %llu, code: %hhu, ha: %hu, va: %hu",
        mdtd->pixel_clock, code, mdtd->h_active, mdtd->v_active);
    if (mdtd->pixel_clock == 0) {
        dpu_pr_warn("[EDID] Empty Timing");
        return false;
    }

    /*
     * For certain VICs the spec allows the vertical
     * front porch to vary by one or two lines.
     */
    vary_vfp(mdtd, code, refresh_rate);

    return true;
}

static int parse_timing_description_by_vesaid(struct edid_video_timing *vid_info, uint8_t vesa_id, bool is_native)
{
    struct dtd mdtd = {0};
    int ret = 0;

    dpu_pr_info("[EDID] video mode value %u", vesa_id);
    if (!convert_code_to_dtd(&mdtd, vesa_id, 60000, CTA)) {
        dpu_pr_warn("[EDID] Invalid video mode value %u", vesa_id);
        return -EINVAL;
    }

    if (mdtd.interlaced == 1) {
        dpu_pr_warn("[EDID] Don't Support interlace mode %u", vesa_id);
        return -EINVAL;
    }

    /* Get Max Value by comparing all values */
    if ((mdtd.h_active > vid_info->max_hpixels) && (mdtd.v_active >= vid_info->max_vpixels) &&
        (mdtd.pixel_clock > vid_info->max_pixel_clock)) {
        vid_info->max_hpixels = mdtd.h_active;
        vid_info->max_vpixels = mdtd.v_active;
        vid_info->max_pixel_clock = mdtd.pixel_clock;
    }

    ret = dtd_timing_info_add_list(vid_info, &mdtd, is_native, false);
    if (ret != 0) {
        dpu_pr_err("[EDID] add timing information to video_timing_list fail !");
        return -EINVAL;
    }

    return 0;
}

static int parse_extension_video_tag(struct edid_video_timing *vid_info, const uint8_t *cea_data_block, uint8_t length)
{
    uint8_t i = 0;
    struct ext_timing *ext_timing_node = NULL;

    for (i = 0; i < length; i++) {
        if (EXTEN_VIDEO_CODE(cea_data_block) != 0) {
            ext_timing_node = kzalloc(sizeof(struct ext_timing), GFP_KERNEL);
            if (ext_timing_node == NULL) {
                dpu_pr_err("[EDID] ext_timing_node memory alloc fail");
                return -EINVAL;
            }
            /* Set up SVD fields */
            ext_timing_node->ext_format_code = EXTEN_VIDEO_CODE(cea_data_block);
            vid_info->ext_vcount += 1;
            if (EXTEN_VIDEO_NATIVE(cea_data_block) == 1)
                parse_timing_description_by_vesaid(vid_info, ext_timing_node->ext_format_code, true);
            else
                parse_timing_description_by_vesaid(vid_info, ext_timing_node->ext_format_code, false);

            list_add_tail(&ext_timing_node->list_node, &vid_info->ext_timing);
        }
        cea_data_block += 1;
    }
    return 0;
}

static int parse_hdmi_vic_id(const uint8_t vic_id)
{
    struct hdmi_vic *node = NULL;

    if (g_hdmi_vic_real_len >= g_hdmi_vic_len) {
        dpu_pr_err("[EDID] The g_hdmi_vic_real_len is more than g_hdmi_vic_len.");
        return -EINVAL;
    }

    if (g_hdmi_vic_list == NULL) {
        g_hdmi_vic_list = kzalloc(sizeof(struct list_head), GFP_KERNEL);
        if (g_hdmi_vic_list == NULL) {
            dpu_pr_err("[EDID] g_hdmi_vic_list is NULL");
            return -EINVAL;
        }
        INIT_LIST_HEAD(g_hdmi_vic_list);
    }

    node = kzalloc(sizeof(struct hdmi_vic), GFP_KERNEL);
    if (node != NULL) {
        node->vic_id = vic_id;
        list_add_tail(&node->list_node, g_hdmi_vic_list);
        g_hdmi_vic_real_len++;
    } else {
        dpu_pr_err("[EDID] kzalloc struct hdmi_vic fail!");
    }

    return 0;
}

static void parse_extension_vsdb_after_latency(const uint8_t *cea_data_block, uint8_t length,
    uint8_t pos_after_latency)
{
    uint8_t i;
    bool b3dpresent = false;

    for (i = pos_after_latency; i < length; i++) {
        if (i == pos_after_latency) {
            b3dpresent = (cea_data_block[i] & 0x80) >> 7;
            dpu_pr_info("[EDID] b3dpresent value is %d.", b3dpresent);
        } else if (i == pos_after_latency + 1) {
            g_hdmi_vic_len = (cea_data_block[i] & 0xE0) >> 5;
            if (g_hdmi_vic_len == 0) {
                dpu_pr_info("[EDID] This EDID don't include HDMI additional video format (2).");
                return;
            }
            g_hdmi_vic_real_len = 0;
        } else if (i <= pos_after_latency + 1 + g_hdmi_vic_len) {
            parse_hdmi_vic_id(cea_data_block[i]);
        } else {
            return;
        }
    }
}

static void parse_hdmi_vsdb_timing_list(struct edid_video_timing *vid_info)
{
    struct hdmi_vic *hdmi_vic_node = NULL;
    struct hdmi_vic *_node_ = NULL;

    list_for_each_entry_safe(hdmi_vic_node, _node_, g_hdmi_vic_list, list_node) {
        switch (hdmi_vic_node->vic_id) {
        case 1:
            parse_timing_description_by_vesaid(vid_info, 95, false);
            break;
        case 2:
            parse_timing_description_by_vesaid(vid_info, 94, false);
            break;
        case 3:
            parse_timing_description_by_vesaid(vid_info, 93, false);
            break;
        case 4:
            parse_timing_description_by_vesaid(vid_info, 98, false);
            break;
        default:
            dpu_pr_warn("[EDID] hdmi_vic_node is illegal!");
            break;
        }
        list_del(&hdmi_vic_node->list_node);
        kfree(hdmi_vic_node);
        hdmi_vic_node = NULL;
    }
    kfree(g_hdmi_vic_list);
    g_hdmi_vic_list = NULL;
}

static int parse_extension_vsdb_tag(struct edid_video_timing *vid_info, const uint8_t *cea_data_block, uint8_t length)
{
    uint8_t i = 0;
    uint32_t ieee_flag = 0;
    uint32_t hdmi_cec_port = 0;
    uint8_t max_tmds_clock = 0;
    uint8_t latency_fields = 0;
    uint8_t interlaced_latency_fields = 0;
    uint8_t hdmi_video_present = 0;
    bool support_ai = false;

    g_hdmi_vic_real_len = 0;
    g_hdmi_vic_len = 0;

    ieee_flag = (cea_data_block[0]) | (cea_data_block[1] << 8) | (cea_data_block[2] << 16);
    if (ieee_flag != 0x000c03) {
        dpu_pr_warn("[EDID] This block isn't belong to HDMI block: %x.", ieee_flag);
        return 0;
    }

    hdmi_cec_port = ((cea_data_block[3] << 8) | (cea_data_block[4]));
    dpu_pr_info("[EDID] hdmi_cec_port value is : %u.", hdmi_cec_port);

    for (i = 5; i < min(length, (uint8_t)8); i++) {
        switch (i) {
            case 5:
                support_ai = (cea_data_block[i] & 0x80) >> 7;
                dpu_pr_info("[EDID] support_ai value is : %d.", support_ai);
                break;
            case 6:
                max_tmds_clock = cea_data_block[i];
                dpu_pr_info("[EDID] max_tmds_clock value is : %u.", max_tmds_clock);
                break;
            case 7:
                latency_fields = (cea_data_block[i] & 0x80) >> 7;
                interlaced_latency_fields = (cea_data_block[i] & 0x40) >> 6;
                hdmi_video_present = (cea_data_block[i] & 0x20) >> 5;
                if (hdmi_video_present == 0) {
                    dpu_pr_warn("[EDID] This EDID don't include HDMI additional video format (1).");
                    return 0;
                }
                break;
            default:
                break;
        }
    }

    if (2 * (latency_fields + interlaced_latency_fields) + 8 < length)
        parse_extension_vsdb_after_latency(cea_data_block, length,
            2 * (latency_fields + interlaced_latency_fields) + 8);

    if (g_hdmi_vic_len == 0)
        return 0;
    dpu_pr_info("[EDID] vic_id real length =%hhu , vic length=%hhu !", g_hdmi_vic_real_len, g_hdmi_vic_len);

    parse_hdmi_vsdb_timing_list(vid_info);

    return 0;
}

static int parse_video_cea_data_block(struct edid_video_timing *video_timing, const uint8_t *cea_data, uint8_t dtd_start)
{
    uint8_t total_length = 4;
    uint8_t block_length = 0;
    uint32_t block_count = 0;
    uint32_t block_tag = 0;
    const uint8_t *cea_data_block = cea_data;

    dpu_pr_info("[EDID] parse_video_cea_data_block +");

    while (total_length < dtd_start) {
        block_tag = get_cea_data_block_tag(cea_data_block);
        block_length = get_cea_data_block_len(cea_data_block);
        dpu_pr_info("[EDID] parse cea data block : block_count: %u; block_tag: %u; block_length: %u.",
            block_count, block_tag, block_length);
        switch (block_tag) {
            case EXTENSION_VIDEO_TAG:
                cea_data_block += 1;
                if (parse_extension_video_tag(video_timing, cea_data_block, block_length)) {
                    dpu_pr_err("[EDID] parse_extension_video_tag fail.");
                    return -EINVAL;
                }
                cea_data_block += block_length;
                break;
            case EXTENSION_VENDOR_TAG:
                cea_data_block += 1;
                if (parse_extension_vsdb_tag(video_timing, cea_data_block, block_length)) {
                    dpu_pr_err("[EDID] parse_extension_vsdb_tag fail.");
                    return -EINVAL;
                }
                cea_data_block += block_length;
                break;
            case EXTENSION_SPEAKER_TAG:
                cea_data_block += 4;
                break;
            default:
                cea_data_block += block_length + 1;
                break;
        }
        total_length = total_length + block_length + 1;
        block_count++;
    }

    return 0;
}

static int parse_audio_cea_data_block(struct edid_audio *out_edid_audio_info, const uint8_t *cea_data, uint8_t dtd_start)
{
    uint8_t total_length = 4;
    uint8_t block_length = 0;
    uint32_t block_count = 0;
    uint32_t block_tag = 0;
    const uint8_t *cea_data_block = cea_data;

    dpu_pr_info("[EDID] parse_audio_cea_data_block +");

    while (total_length < dtd_start) {
        block_tag = get_cea_data_block_tag(cea_data_block);
        block_length = get_cea_data_block_len(cea_data_block);
        dpu_pr_info("[EDID] parse cea data block : block_count: %u; block_tag: %u; block_length: %u.",
            block_count, block_tag, block_length);
        switch (block_tag) {
            case EXTENSION_AUDIO_TAG:
                cea_data_block += 1;
                if (parse_extension_audio_tag(out_edid_audio_info, cea_data_block, block_length)) {
                    dpu_pr_err("[EDID] parse_extension_audio_tag fail.");
                    return -EINVAL;
                }
                cea_data_block += block_length;
                break;
            case EXTENSION_SPEAKER_TAG:
                cea_data_block += 1;
                out_edid_audio_info->ext_speaker = EXTEN_SPEAKER(cea_data_block);
                cea_data_block += 3;
                break;
            default:
                cea_data_block += block_length + 1;
                break;
        }
        total_length = total_length + block_length + 1;
        block_count++;
    }

    return 0;
}

static int parse_extension_video_timing_description(struct edid_video_timing *video_timing,
    const uint8_t *dtd_block, uint32_t dtd_begin, uint16_t dtd_total, const uint8_t *edid)
{
    uint32_t i = 0;
    int ret = 0;

    if ((dtd_begin + 1 + dtd_total * DETAILED_TIMING_DESCRIPTION_SIZE) > EDID_LENGTH) {
        dpu_pr_err("[EDID] The dtd total number 0x%x is out of the limit", dtd_total);
        return -EINVAL;
    }

    for (i = 0; i < (uint32_t)dtd_total; i++, dtd_block += DETAILED_TIMING_DESCRIPTION_SIZE) {
        switch (get_block_type(dtd_block)) {
            case DETAILED_TIMING_BLOCK:
                ret = parse_video_timing_description(video_timing, dtd_block, edid);
                if (ret != 0) {
                    dpu_pr_err("[EDID] Timing Description Parsing failed!");
                    return ret;
                }
                break;
            case MONITOR_LIMITS:
                parse_monitor_video_limits(video_timing, dtd_block);
                break;
            default:
                break;
        }
    }
    return 0;
}

static int get_cea_block_info(const uint8_t *exten, struct cea_block_info *cea_info)
{
    int ret = 0;

    dpu_pr_info("[EDID] get_cea_block_info +");

    ret = check_exten_edid(exten);
    if (ret) {
        dpu_pr_warn("[EDID] The check_exten_edid failed.");
        return ret;
    }

    if (exten[2] == 0x00) {
        dpu_pr_info("[EDID] There are no DTDs present in this extension block and no non-DTD data.");
        return -EINVAL;
    } else if (exten[2] == 0x04) {
        cea_info->dtd_start_byte = 0x04;
    } else {
        cea_info->cea_data_block_collection = 0x04;
        cea_info->dtd_start_byte = exten[2];
    }

    if (cea_info->cea_data_block_collection == 0x04) {
        cea_info->cea_block = exten + cea_info->cea_data_block_collection;
    }

    cea_info->reversion = exten[1];
    cea_info->dtd_total = lower_nibble(exten[3]);
    cea_info->dtd_block = exten + cea_info->dtd_start_byte;

    dpu_pr_info("[EDID] parse video_extension head: reversion: %u; dtd_start_byte: %u; dtd_total: %u.",
        cea_info->reversion, cea_info->dtd_start_byte, cea_info->dtd_total);

    if (cea_info->dtd_total != (EDID_LENGTH - cea_info->dtd_start_byte - 1) / DETAILED_TIMING_DESCRIPTION_SIZE) {
        dpu_pr_info("[EDID] The number of native DTDs is not equal the size");
        cea_info->dtd_total = (EDID_LENGTH - cea_info->dtd_start_byte - 1) / DETAILED_TIMING_DESCRIPTION_SIZE;
    }

    dpu_pr_info("[EDID] get_cea_block_info -");
    return 0;
}

static int parse_video_cea_extension(struct edid_video_timing *video_timing, const uint8_t *exten, const uint8_t *edid)
{
    int ret = 0;
    struct cea_block_info cea_info = {0};

    dpu_pr_info("[EDID] parse_video_cea_extension +");

    ret = get_cea_block_info(exten, &cea_info);
    if (ret != 0) {
        dpu_pr_err("[EDID] get cea block info failed!");
        return ret;
    }

    /* Parse CEA Data Block Collection */
    if (cea_info.cea_data_block_collection == 0x04) {
        ret = parse_video_cea_data_block(video_timing, cea_info.cea_block, cea_info.dtd_start_byte);
        if (ret != 0) {
            dpu_pr_err("[EDID] video CEA data block Parsing failed!");
            return ret;
        }
    }

    // Parse DTD in Extension
    ret = parse_extension_video_timing_description(video_timing, cea_info.dtd_block,
        cea_info.dtd_start_byte, cea_info.dtd_total, edid);
    if (ret) {
        dpu_pr_err("[EDID] Parse video extension block timing information fail.");
        return ret;
    }

    dpu_pr_info("[EDID] parse_video_cea_extension -");
    return 0;
}

static int16_t parse_cea_eeodb(const uint8_t *edid, const uint32_t edid_len)
{
	int16_t tmp_block_num = 0;
	int16_t ext_block_num = 1;

	if (edid[EDID_LENGTH] == EXTENSION_HEADER_CEA &&
		edid[EDID_LENGTH + CTA_EEODB_FLAG_INDEX1] == CTA_EEODB_FLAG_ID1 &&
		edid[EDID_LENGTH + CTA_EEODB_FLAG_INDEX2] == CTA_EEODB_FLAG_ID2) {
		tmp_block_num = (int16_t)edid[EDID_LENGTH + CTA_EEODB_LENGTH_INDEX];
		dpu_pr_info("[EDID] tmp_block_num=%d", tmp_block_num);
	}

	if (tmp_block_num > 1 && tmp_block_num < EDID_MAX_BLOCK_NUM &&
		(edid_len == (uint32_t)((tmp_block_num + 1) * EDID_LENGTH))) {
		ext_block_num = tmp_block_num;
		dpu_pr_info("[EDID] ext_block_num=%d", ext_block_num);
	}

	return ext_block_num;
}

bool parse_edid_video_info(const uint8_t *edid, const uint32_t edid_len, struct edid_video *out_edid_video_info)
{
    int16_t i = 0;
    int16_t ext_block_num = 0;
    int ret = 0;
    struct edid_video_timing *video_timing = NULL;
    int ext_timing_num = 0;

    dpu_check_and_return(!edid, false, err, "[EDID] edid is NULL!");
    dpu_check_and_return(!out_edid_video_info, false, err, "[EDID] out_edid_video_info is NULL!");

    video_timing = &out_edid_video_info->video_timing;

    /* Parse the EDID main part, check how many(count as ' ext_block_num ') Extension blocks there are to follow. */
    ext_block_num = parse_main(edid, out_edid_video_info);
    if (ext_block_num > EDID_MAX_BLOCK_NUM) {
        dpu_pr_warn("[EDID] ext_block_num is invalid");
        return false;
    }

	if (ext_block_num == 1 && (edid_len > (uint32_t)((ext_block_num + 1) * EDID_LENGTH))) {
		dpu_pr_info("[EDID] parse cea eeodb");
		ext_block_num = parse_cea_eeodb(edid, edid_len);
	}

    dpu_pr_info("[EDID] ext_block_num is %d, edid_len is %hu", ext_block_num, edid_len);
    if ((ext_block_num > 0) && (edid_len >= (uint32_t)((ext_block_num + 1) * EDID_LENGTH))) {
        /* Parse video Extension blocks */
        for (i = 0; i < ext_block_num; i++) {
            dpu_pr_info("[EDID] pasrse ext_block_num : %d", i);
            switch (edid[(EDID_LENGTH * (i + 1))]) {
                case EXTENSION_HEADER_DISPLAYID:
                    ret = parse_video_displayid_extension(video_timing, edid + (EDID_LENGTH * (i + 1)));
                    break;
                case EXTENSION_HEADER_CEA:
                    ret = parse_video_cea_extension(video_timing, edid + (EDID_LENGTH * (i + 1)), edid);
                    break;
                default:
                    dpu_pr_warn("[EDID] block tag mismatched!");
                    ret = -1;
                    break;
            }
            if (ret != 0) {
                dpu_pr_warn("[EDID] Extension Video Parsing failed!");
            }
        }
    } else if (ext_block_num < 0) {
        dpu_pr_err("[EDID] Error occurred while parsing main block");
        return false;
    }

    /* Parse the Established Timings in EDID Block0 */
    ext_timing_num = parse_established_timing(edid, video_timing);
    dpu_pr_info("[EDID] total established timing num: %d", ext_timing_num);
	/* Parse the Standard Timings in EDID Block0 */
	ext_timing_num = parse_standard_timing(edid, video_timing);
	dpu_pr_info("[EDID] total standard timing num:%d", ext_timing_num);

    dpu_pr_info("[EDID] parse_edid_video_info -");
    return true;
}

static int parse_audio_extension(struct edid_audio *out_edid_audio_info, const uint8_t *exten)
{
    int ret = 0;
    struct cea_block_info cea_info = {0};

    dpu_pr_info("[EDID] parse_audio_extension +");

    ret = get_cea_block_info(exten, &cea_info);
    if (ret != 0) {
        dpu_pr_err("[EDID] get cea block info failed!");
        return ret;
    }

    /* Parse CEA Data Block Collection */
    if (cea_info.cea_data_block_collection == 0x04) {
        ret = parse_audio_cea_data_block(out_edid_audio_info, cea_info.cea_block, cea_info.dtd_start_byte);
        if (ret != 0) {
            dpu_pr_err("[EDID] audio CEA data block Parsing failed!");
            return ret;
        }
    }

    if (out_edid_audio_info->basic_audio != 1)
        out_edid_audio_info->basic_audio = (0x40 & exten[3]) >> 6;

    dpu_pr_info("[EDID] parse_audio_extension -");
    return 0;
}

bool parse_edid_audio_info(const uint8_t *edid, const uint32_t edid_len, struct edid_audio *out_edid_audio_info)
{
    int16_t i = 0;
    int16_t ext_block_num = 0;
    int ret = 0;

    dpu_pr_info("[EDID] parse_edid_audio_info +");

    dpu_check_and_return(!edid, false, err, "[EDID] edid is NULL!");
    dpu_check_and_return(!out_edid_audio_info, false, err, "[EDID] out_edid_audio_info is NULL!");

    ext_block_num = (int16_t)edid[EXTENSION_FLAG];

    if ((ext_block_num > 0) && (edid_len >= (uint32_t)((ext_block_num + 1) * EDID_LENGTH))) {
        /* Parse audio Extension blocks */
        for (i = 0; i < ext_block_num; i++) {
            dpu_pr_info("[EDID] pasrse ext_block_num : %d", i);
            switch (edid[(EDID_LENGTH * (i + 1))]) {
                case EXTENSION_HEADER_CEA:
                    ret = parse_audio_extension(out_edid_audio_info, edid + (EDID_LENGTH * (i + 1)));
                    break;
                default:
                    dpu_pr_warn("[EDID] block tag mismatched!");
                    ret = -1;
                    break;
            }
            if (ret != 0) {
                dpu_pr_warn("[EDID] Extension Audio Parsing failed!");
            }
        }
    }

    dpu_pr_info("[EDID] parse_edid_audio_info -");
    return true;
}

static void print_parsed_edid_info(struct edid_information *out_edid_info)
{
    uint32_t i = 0;
    struct edid_timing_node *timing_info_node = NULL;
    struct edid_timing_node *tmp_node = NULL;
    struct ext_timing *ext_timing_node = NULL;
    struct ext_timing *tmp_ext_node = NULL;
    struct edid_audio_info *audio_info_node = NULL;
    struct edid_audio_info *tmp_audio_node = NULL;

    dpu_pr_info("[EDID] +++++++++++++++++++++++++++++++++");
    dpu_pr_info("out_edid_info->edid_video : ");
    dpu_pr_info("  main_vcount : %u", out_edid_info->video.video_timing.main_vcount);
    dpu_pr_info("  ext_vcount : %u", out_edid_info->video.video_timing.ext_vcount);
    dpu_pr_info("  pri_vcount : %u", out_edid_info->video.video_timing.pri_vcount);
    dpu_pr_info("  max_hpixels : %u", out_edid_info->video.video_timing.max_hpixels);
    dpu_pr_info("  max_vpixels : %u", out_edid_info->video.video_timing.max_vpixels);
    dpu_pr_info("  max_himage_size : %u", out_edid_info->video.video_timing.max_himage_size);
    dpu_pr_info("  max_vimage_size : %u", out_edid_info->video.video_timing.max_vimage_size);
    dpu_pr_info("  max_vfreq : %u", out_edid_info->video.video_timing.max_vfreq);
    dpu_pr_info("  min_vfreq : %u", out_edid_info->video.video_timing.min_vfreq);
    dpu_pr_info("  max_hfreq : %u", out_edid_info->video.video_timing.max_hfreq);
    dpu_pr_info("  min_hfreq : %u", out_edid_info->video.video_timing.min_hfreq);
    dpu_pr_info("  max_pixel_clock : %lu", out_edid_info->video.video_timing.max_pixel_clock);

    for (i = 0; i < out_edid_info->video.video_timing.pri_vcount; i++)
        dpu_pr_info("  pri_timing[%u] : pri_hpixels %u ; pri_vpixels %u ; pri_fps : %u",
            i, out_edid_info->video.video_timing.pri_timing[i].pri_hpixels,
            out_edid_info->video.video_timing.pri_timing[i].pri_vpixels,
            out_edid_info->video.video_timing.pri_timing[i].pri_fps);

    list_for_each_entry_safe(timing_info_node, tmp_node, &out_edid_info->video.video_timing.video_timing_list, list_node) {
        if (!timing_info_node) {
            dpu_pr_err("[EDID] timing_info_node is null");
            continue;
        }
        dpu_pr_info("[EDID] timing_info_node : hactive_pixels is %hu, vactive_pixels is %hu, fps = %u, pixel clock=%llu",
            timing_info_node->timing_info.hactive_pixels, timing_info_node->timing_info.vactive_pixels,
            timing_info_node->timing_info.fps, timing_info_node->timing_info.pixel_clock);
    }

    list_for_each_entry_safe(ext_timing_node, tmp_ext_node, &out_edid_info->video.video_timing.ext_timing, list_node) {
        if (!ext_timing_node) {
            dpu_pr_err("[EDID] ext_timing_node is null");
            continue;
        }
        dpu_pr_info("[EDID] ext_timing_node: ext_format_code is %hu, ext_hpixels is %hu, ext_vpixels = %u, ext_vfreq = %llu",
            ext_timing_node->ext_format_code, ext_timing_node->ext_hpixels,
            ext_timing_node->ext_vpixels, ext_timing_node->ext_vfreq);
    }

    dpu_pr_info("[EDID] +++++++++++++++++++++++++++++++++");
    dpu_pr_info("out_edid_info->edid_audio : ");
    dpu_pr_info("  basic_audio : %u", out_edid_info->audio.basic_audio);
    dpu_pr_info("  ext_speaker : %u", out_edid_info->audio.ext_speaker);
    dpu_pr_info("  ext_acount : %u", out_edid_info->audio.ext_acount);
    list_for_each_entry_safe(audio_info_node, tmp_audio_node, &out_edid_info->audio.spec, list_node) {
        if (!audio_info_node) {
            dpu_pr_err("[EDID] audio_info_node is null");
            continue;
        }
        dpu_pr_info("[EDID] audio_info_node: format is %hu, channels is %hu, sampling = %u, bitrate=%llu",
            audio_info_node->format, audio_info_node->channels, audio_info_node->sampling, audio_info_node->bitrate);
    }
    dpu_pr_info("[EDID] +++++++++++++++++++++++++++++++++");
}

static void print_edid(uint8_t *edid, const uint32_t edid_len)
{
    uint32_t i = 0;
    uint32_t j = 0;
    uint8_t *block = NULL;
    uint32_t str_start = 5;
    uint32_t str_end = 0;
    uint8_t data_tmp = 0;

    for (i = EDID_FACTORY_START; i <= EDID_FACTORY_END; i++)
        edid[i] = 0x00;

    block = edid + DETAILED_TIMING_DESCRIPTIONS_START;
    for (i = 0; i < DETAILED_TIMING_DESCRIPTION_COUNT; i++, block += DETAILED_TIMING_DESCRIPTION_SIZE) {
        switch (get_block_type(block)) {
        case MONITOR_NAME:
            for (j = str_start; j < (DETAILED_TIMING_DESCRIPTION_SIZE - 1); j++) {
                if ((block[j] == 0x0A) && (block[j + 1] == 0x20)) {
                    str_end = j;
                    break;
                }
            }
            if (str_end == 0)
                str_end = DETAILED_TIMING_DESCRIPTION_SIZE;
            for (j = str_start; j < str_end; j++)
                data_tmp += block[j];
            if (memset_s(&block[str_start], MONITOR_NAME_DESCRIPTION_SIZE - 1, 0x00,
                MONITOR_NAME_DESCRIPTION_SIZE - 1) != EOK) {
                dpu_pr_warn("[EDID] Failed to memset monitor name momery");
            }
            block[str_start] = data_tmp;
            break;
        case MONITOR_SERIAL_NUMBER:
            if (memset_s(&block[str_start], MONITOR_NUMBER_DESCRIPTION_SIZE - 1, 0x00,
                MONITOR_NUMBER_DESCRIPTION_SIZE - 1) != EOK) {
                dpu_pr_warn("[EDID] Failed to memset monitor serial number momery");
            }
            break;
        default:
            break;
        }
    }

    for (i = 0; i < edid_len;) {
        if (!(i % 16)) {
            dpu_pr_info("EDID [%04x]:  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                i, edid[i], edid[i + 1], edid[i + 2], edid[i + 3],
                edid[i + 4], edid[i + 5], edid[i + 6], edid[i + 7],
                edid[i + 8], edid[i + 9], edid[i + 10], edid[i + 11],
                edid[i + 12], edid[i + 13], edid[i + 14], edid[i + 15]);
        }

        i += 16;
        if (i == 128)
            dpu_pr_info("<<<-------------------------------------------------------------->>>");
    }
}

static bool check_edid_block(const uint8_t *edid, const uint32_t edid_len)
{
    if (((edid_len / EDID_LENGTH) > EDID_MAX_BLOCK_NUM) || ((edid_len % EDID_LENGTH) != 0) ||
        (edid_len < EDID_LENGTH)) {
        dpu_pr_err("[EDID] Raw Data length is invalid(not the size of (128 x N , N = [1-5]) uint8_t!");
        return false;
    }

    if (check_main_edid(edid) != 0) {
        dpu_pr_warn("[EDID] The main edid block is wrong");
        return false;
    }

    return true;
}

/* this function would be deleted after the mipi bridge driver updates */
static void copy_timing_to_edid_timing_info_list(struct edid_video *out_edid_video_info)
{
	uint8_t cnt = 0;
	struct edid_timing_info *target_node = NULL;
	struct edid_timing_node *origin_node = NULL;
	struct edid_timing_node *temp_node = NULL;
	struct edid_video_timing *video_timing = NULL;

	video_timing = &out_edid_video_info->video_timing;

	list_for_each_entry_safe(origin_node, temp_node, &video_timing->video_timing_list, list_node) {
		dpu_pr_warn("[EDID] copy timing [%u] to edid_timing_info_list", cnt);
		target_node = kzalloc(sizeof(struct edid_timing_node), GFP_KERNEL);
		if (target_node == NULL) {
			dpu_pr_warn("[EDID] kzalloc target_node failed");
			return;
		}

		target_node->hactive_pixels = origin_node->timing_info.hactive_pixels;
		target_node->hblanking = origin_node->timing_info.hblanking;
		target_node->hsync_offset = origin_node->timing_info.hsync_offset;
		target_node->hsync_pulse_width = origin_node->timing_info.hsync_pulse_width;
		target_node->hsize = origin_node->timing_info.hsize;
		target_node->hsync_polarity = origin_node->timing_info.hsync_polarity;
		target_node->hborder = origin_node->timing_info.hborder;

		target_node->vactive_pixels = origin_node->timing_info.vactive_pixels;
		target_node->vblanking = origin_node->timing_info.vblanking;
		target_node->vsync_offset = origin_node->timing_info.vsync_offset;
		target_node->vsync_pulse_width = origin_node->timing_info.vsync_pulse_width;
		target_node->vsize = origin_node->timing_info.vsize;
		target_node->vsync_polarity = origin_node->timing_info.vsync_polarity;
		target_node->vborder = origin_node->timing_info.vborder;

		target_node->pixel_clock = origin_node->timing_info.pixel_clock / 10;
		target_node->fps = origin_node->timing_info.fps;
		target_node->interlaced = origin_node->timing_info.interlaced;
		target_node->input_type = origin_node->timing_info.input_type;
		target_node->scheme_detail = origin_node->timing_info.scheme_detail;
		target_node->sync_scheme = origin_node->timing_info.sync_scheme;

		list_del(&origin_node->list_node);
		kfree(origin_node);
		origin_node = NULL;
		list_add_tail(&target_node->list_node, &video_timing->video_timing_list);
		cnt++;
		if (cnt == video_timing->main_vcount)
			break;
	}
}

bool parse_edid_info(const uint8_t *edid, const uint32_t edid_len, struct edid_information *out_edid_info)
{
    int i = 0;
    uint8_t *edid_backup = NULL;
	bool is_copy_link = true;

    dpu_pr_info("[EDID] parse_edid_info +");

    dpu_check_and_return(!edid, false, err, "[EDID] Raw Data is invalid!(NULL error)!");
    dpu_check_and_return(!out_edid_info, false, err, "[EDID] out_edid_info is invalid!(NULL error)!");

    if (!check_edid_block(edid, edid_len)) {
        dpu_pr_err("[EDID] edid block check failed!");
        return false;
    }

    edid_backup = (uint8_t *)kzalloc(edid_len * sizeof(uint8_t), GFP_KERNEL);
    if (edid_backup == NULL) {
        dpu_pr_err("[EDID] Failed to allocate buffer for edid");
        return false;
    }
    for (i = 0; i < edid_len; i++)
        edid_backup[i] = edid[i];

    /* Parse the EDID product info */
    parse_edid_product_info(edid_backup, edid_len, &out_edid_info->product_info);

    /* Parse the EDID VIDEO part. */
    if (!parse_edid_video_info(edid_backup, edid_len, &out_edid_info->video)) {
        dpu_pr_err("[EDID] Error occurred while parsing video edid!");
        print_parsed_edid_info(out_edid_info);
        kfree(edid_backup);
        return false;
    }

	if (is_copy_link)
		copy_timing_to_edid_timing_info_list(&out_edid_info->video);

    /* Parse the EDID AUDIO part. */
    if (!parse_edid_audio_info(edid_backup, edid_len, &out_edid_info->audio)) {
        dpu_pr_err("[EDID] Error occurred while parsing audio edid!");
        print_parsed_edid_info(out_edid_info);
        kfree(edid_backup);
        return false;
    }

    print_edid(edid_backup, edid_len);
    print_parsed_edid_info(out_edid_info);
    kfree(edid_backup);
    dpu_pr_info("[EDID] parse_edid_info -");
    return true;
}

static void copy_edid_timing_from_link_to_arrary(struct dkmd_object_info *object_info, struct edid_video_timing *video_timing)
{
	uint32_t cnt = 0;
	struct edid_timing_node *node = NULL;
	struct edid_timing_node *temp_node = NULL;

	list_for_each_entry_safe(node, temp_node, &video_timing->video_timing_list, list_node) {
		if (node == NULL) {
			dpu_pr_err("[EDID] edid_timing_node is NULL");
			continue;
		}
		object_info->timing_list[cnt].hactive_pixels = node->timing_info.hactive_pixels;
		object_info->timing_list[cnt].hblanking = node->timing_info.hblanking;
		object_info->timing_list[cnt].hsync_offset = node->timing_info.hsync_offset;
		object_info->timing_list[cnt].hsync_pulse_width = node->timing_info.hsync_pulse_width;
		object_info->timing_list[cnt].hsize = node->timing_info.hsize;
		object_info->timing_list[cnt].hsync_polarity = node->timing_info.hsync_polarity;
		object_info->timing_list[cnt].hborder = node->timing_info.hborder;

		object_info->timing_list[cnt].vactive_pixels = node->timing_info.vactive_pixels;
		object_info->timing_list[cnt].vblanking = node->timing_info.vblanking;
		object_info->timing_list[cnt].vsync_offset = node->timing_info.vsync_offset;
		object_info->timing_list[cnt].vsync_pulse_width = node->timing_info.vsync_pulse_width;
		object_info->timing_list[cnt].vsize = node->timing_info.vsize;
		object_info->timing_list[cnt].vsync_polarity = node->timing_info.vsync_polarity;
		object_info->timing_list[cnt].vborder = node->timing_info.vborder;

		object_info->timing_list[cnt].pixel_clock = node->timing_info.pixel_clock;
		object_info->timing_list[cnt].fps = node->timing_info.fps;
		object_info->timing_list[cnt].interlaced = node->timing_info.interlaced;
		object_info->timing_list[cnt].input_type = node->timing_info.input_type;
		object_info->timing_list[cnt].scheme_detail = node->timing_info.scheme_detail;
		object_info->timing_list[cnt].sync_scheme = node->timing_info.sync_scheme;
		object_info->timing_list[cnt].preferred = node->timing_info.preferred;

		cnt++;
		if (cnt >= MAX_TIMING_NUM) {
			dpu_pr_warn("[EDID] Timing number reaches %u and max timing_list number should be extened", cnt);
			break;
		}
	}

	object_info->timing_num = cnt;
	dpu_pr_info("[EDID] total edid timing num is %u", object_info->timing_num);
}

static void edid_parse_aspect_ratio(const uint8_t *edid, struct dkmd_connector_info *pinfo)
{
	uint8_t hscreen_size = 0;
	uint8_t vscreen_size = 0;
	uint8_t edid_version = 0;
	uint8_t edid_revision = 0;
	uint8_t *h_ori = "landscape orientation";
	uint8_t *v_ori = "portrait orientation";
	uint8_t *ori = NULL;

	hscreen_size = edid[HORIZONTAL_SCREEN_SIZE];
	vscreen_size = edid[VERTICAL_SCREEN_SIZE];
	edid_version = edid[EDID_STRUCT_VERSION];
	edid_revision = edid[EDID_STRUCT_REVISION];

	pinfo->base.screen_aspect_ratio = 0;
	pinfo->base.screen_orientation = NONE_ORIENTATION;
	if (hscreen_size == 0 && vscreen_size == 0) {
		dpu_pr_info("[EDID] the manufacturer does not specify the display's screen size");
		return;
	}

	dpu_pr_info("[EDID] the screen size is [%u * %u]", hscreen_size, vscreen_size);
	if (edid_version > 1 || (edid_version == 1 && edid_revision >= 4)) {
		if (hscreen_size != 0 && vscreen_size == 0) {
			pinfo->base.screen_orientation = LANDSCAPE;
			pinfo->base.screen_aspect_ratio = hscreen_size + 99;
			dpu_pr_info("[EDID] the aspect ratio(h/v) of %s is:%u", h_ori, pinfo->base.screen_aspect_ratio);
		}
		if (hscreen_size == 0 && vscreen_size != 0) {
			pinfo->base.screen_orientation = PORTRAIT;
			pinfo->base.screen_aspect_ratio = vscreen_size + 99;
			dpu_pr_info("[EDID] the aspect ratio(v/h) of %s is:%u", v_ori, pinfo->base.screen_aspect_ratio);
		}
	}

	if (hscreen_size != 0 && vscreen_size != 0) {
		if (hscreen_size >= vscreen_size) {
			pinfo->base.screen_orientation = LANDSCAPE;
			pinfo->base.screen_aspect_ratio = IMAGE_RATIO(hscreen_size, vscreen_size);
			ori = h_ori;
		} else {
			pinfo->base.screen_orientation = PORTRAIT;
			pinfo->base.screen_aspect_ratio = IMAGE_RATIO(vscreen_size, hscreen_size);
			ori = v_ori;
		}
		dpu_pr_info("[EDID] the aspect ratio of %s screen is approximately %u", ori, pinfo->base.screen_aspect_ratio);
	}
}

bool edid_parse_info(const uint8_t *edid, const uint32_t edid_len, struct dkmd_connector_info *pinfo,
		struct edid_information *out_edid_info)
{
    int i = 0;
    uint8_t *edid_backup = NULL;

    dpu_pr_info("[EDID] parse_edid_info +");

    dpu_check_and_return(!edid, false, err, "[EDID] Raw Data is invalid!(NULL error)!");
	dpu_check_and_return(!pinfo, false, err, "[EDID] dkmd_connector_info is invalid!(NULL error)!");
    dpu_check_and_return(!out_edid_info, false, err, "[EDID] out_edid_info is invalid!(NULL error)!");

    if (!check_edid_block(edid, edid_len)) {
        dpu_pr_err("[EDID] edid block check failed!");
        return false;
    }

    edid_backup = (uint8_t *)kzalloc(edid_len * sizeof(uint8_t), GFP_KERNEL);
    if (edid_backup == NULL) {
        dpu_pr_err("[EDID] Failed to allocate buffer for edid");
        return false;
    }
    for (i = 0; i < edid_len; i++)
        edid_backup[i] = edid[i];

    /* Parse the EDID product info */
    parse_edid_product_info(edid_backup, edid_len, &out_edid_info->product_info);

    /* Parse the EDID VIDEO part. */
    if (!parse_edid_video_info(edid_backup, edid_len, &out_edid_info->video)) {
        dpu_pr_err("[EDID] Error occurred while parsing video edid!");
        print_parsed_edid_info(out_edid_info);
        kfree(edid_backup);
        return false;
    }

	/* Parse the horizontal and vertical screen size or aspect ratio */
	edid_parse_aspect_ratio(edid, pinfo);

	copy_edid_timing_from_link_to_arrary(&pinfo->base, &out_edid_info->video.video_timing);

	if (dpu_conn_edid_timing_filter(pinfo, edid_backup) != 0) {
		dpu_pr_err("[EDID] Error occurred while filter edid timing!");
		print_parsed_edid_info(out_edid_info);
        kfree(edid_backup);
        return false;
	}

    /* Parse the EDID AUDIO part. */
    if (!parse_edid_audio_info(edid_backup, edid_len, &out_edid_info->audio)) {
        dpu_pr_err("[EDID] Error occurred while parsing audio edid!");
        print_parsed_edid_info(out_edid_info);
        kfree(edid_backup);
        return false;
    }

    print_edid(edid_backup, edid_len);
    print_parsed_edid_info(out_edid_info);
    kfree(edid_backup);
    dpu_pr_info("[EDID] parse_edid_info -");
    return true;
}
