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

#include "edid_private.h"
#include "dkmd_log.h"
#include "securec.h"
#include "edid_timing_select.h"
#include "edid_timing_config.h"

struct edid_hook_ops *g_edid_hook_ops = NULL;

static uint32_t edid_calculate_aspect_ratio(uint16_t hsize, uint16_t vsize)
{
	return (hsize >= vsize ? IMAGE_RATIO(hsize, vsize) : IMAGE_RATIO(vsize, hsize));
}

static bool edid_check_supported_fps(uint16_t target_fps, uint16_t cur_fps)
{
	return ((target_fps - cur_fps <= 1) && (target_fps - cur_fps >= 0));
}

static bool edid_timing_is_progressive_mode(struct edid_timing_info *timing_info)
{
	if (timing_info->interlaced != PROGRESSIVE) {
		dpu_pr_info("[EDID] timing[%u * %u @ %u] is invalid for vscan_mode is %u", timing_info->hactive_pixels,
			timing_info->vactive_pixels, timing_info->fps, timing_info->interlaced);
		return false;
	}

	return true;
}

static bool edid_timing_resolution_is_over_upper_limit(struct edid_timing_info *timing_info)
{
	if (timing_info->hactive_pixels > MAX_HACTIVE) {
		dpu_pr_info("[EDID] timing[%u * %u @ %u] is invalid for hactive_pixels than max active pixels.",
			timing_info->hactive_pixels, timing_info->vactive_pixels, timing_info->fps);
		return false;
	}

	if (timing_info->vactive_pixels > MAX_HACTIVE) {
		dpu_pr_info("[EDID] timing[%u * %u @ %u] is invalid for vactive_pixels than max active pixels.",
			timing_info->hactive_pixels, timing_info->vactive_pixels, timing_info->fps);
		return false;
	}

	if (timing_info->hactive_pixels > MAX_VACTIVE && timing_info->vactive_pixels > MAX_VACTIVE) {
		dpu_pr_info("[EDID] timing[%u * %u @ %u] is invalid for active_pixels higher than max active pixels.",
			timing_info->hactive_pixels, timing_info->vactive_pixels, timing_info->fps);
		return false;
	}

	return true;
}

static bool edid_timing_fps_supported(struct edid_timing_info *timing_info)
{
	uint16_t max_fps = PREFERRED_MAX_FPS_OF_MONITOR;
	uint16_t pref_fps = PREFERRED_DEFAULT_FPS;
	uint16_t support_fps1 = SUPPORT_FPS1;

	if (timing_info->fps <= 0 || timing_info->fps > max_fps) {
		dpu_pr_info("[EDID] timing[%u * %u @ %u] is invalid for fps is invalid",
			timing_info->hactive_pixels, timing_info->vactive_pixels, timing_info->fps);
		return false;
	}

	if (!edid_check_supported_fps(pref_fps, timing_info->fps) &&
			!edid_check_supported_fps(support_fps1, timing_info->fps)) {
		dpu_pr_info("[EDID] timing[%u * %u @ %u] is invalid for fps is not supported",
			timing_info->hactive_pixels, timing_info->vactive_pixels, timing_info->fps);
		return false;
	}

	return true;
}

static bool edid_timing_vbp_supported(struct edid_timing_info *timing_info)
{
	uint16_t vbp = timing_info->vblanking - timing_info->vsync_offset - timing_info->vsync_pulse_width;

	if (vbp < MIN_VBP || timing_info->vblanking > MAX_VBLANKING) {
		dpu_pr_info("[EDID] timing[%u * %u @ %u] is invalid for vbp[%u] is not proper",
			timing_info->hactive_pixels, timing_info->vactive_pixels, timing_info->fps, vbp);
		return false;
	}

	return true;
}

static bool edid_check_timing_valid(struct dkmd_connector_info *pinfo, struct edid_timing_info *timing_info)
{
	/* Check if the VSCAN mode is not supported, interlaced mode is not supported */
	if (!edid_timing_is_progressive_mode(timing_info))
		return false;

	/* Check if the resolution exceeds the upper limit */
	if (!edid_timing_resolution_is_over_upper_limit(timing_info))
		return false;

	/* Check if the frame rate exceeds the limit */
	if (!edid_timing_fps_supported(timing_info))
		return false;

	/* Check if the vertical blank parameters exceeds the limit */
	if (!edid_timing_vbp_supported(timing_info))
		return false;

	return true;
}

static bool edid_check_same_resolution(struct edid_timing_info *timing_list, struct edid_timing_info *cur_timing_info,
			uint32_t cnt)
{
	uint32_t i = 0;
	uint32_t fps_gap = 1;
	bool retval = false;

	for (i = 0; i < cnt; i++) {
		if (timing_list[i].hactive_pixels == cur_timing_info->hactive_pixels &&
				timing_list[i].vactive_pixels == cur_timing_info->vactive_pixels &&
				(VALUE_DIFF(cur_timing_info->fps, timing_list[i].fps) <= fps_gap)) {
			dpu_pr_info("[EDID] timing[%u * %u @ %u] has been existent in the edid timing list",
				cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps);
			retval = true;
			break;
		}
	}

	return retval;
}

static void edid_sort_timing_by_fps(struct edid_timing_info *timing_list, uint32_t timing_num)
{
	uint32_t i = 0;
	uint32_t j = 0;
	struct edid_timing_info temp_info = {0};

	for (i = 0; i < timing_num; i++) {
		for (j = 1; j < timing_num - i; j++) {
			if (timing_list[j - 1].fps < timing_list[j].fps) {
				temp_info = timing_list[j - 1];
				timing_list[j - 1] = timing_list[j];
				timing_list[j] = temp_info;
			}
		}
	}
}

static bool edid_check_target_aspect_ratio(struct edid_timing_info *cur_timing_info, uint32_t target_screen_ratio,
			uint8_t target_screen_orientation)
{
	uint32_t ratio_gap = 10;
	uint32_t cur_timing_ratio = 0;

	/*
	* If screen orientation of EDID is landscape or portrait, the aspect ratio of portrait or landscape
	* should not be preferred
	*/
	if (target_screen_orientation == LANDSCAPE && cur_timing_info->hactive_pixels < cur_timing_info->vactive_pixels) {
		dpu_pr_info("[EDID] the screen orientation of current timing[%u * %u @ %u] is not landscape",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps);
		return false;
	}
	if (target_screen_orientation == PORTRAIT && cur_timing_info->hactive_pixels >= cur_timing_info->vactive_pixels) {
		dpu_pr_info("[EDID] the screen orientation of current timing[%u * %u @ %u] is not portrait",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps);
		return false;
	}

	/* Check the aspect ratio that is whether approximately same as the target screen ratio */
	cur_timing_ratio = edid_calculate_aspect_ratio(cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels);
	if ((target_screen_ratio > 0) && VALUE_DIFF(target_screen_ratio, cur_timing_ratio) > ratio_gap) {
		dpu_pr_info("[EDID] the aspect ratio[%u] of current timing[%u * %u @ %u] is not same as the target ratio[%u]",
			cur_timing_ratio, cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps,
			target_screen_ratio);
		return false;
	}

	return true;
}

static bool edid_check_same_aspect_ratio(struct edid_timing_info *cur_timing_info,
			struct edid_timing_info *pref_timing_info, uint32_t cur_timing_ratio, uint32_t cur_perf_timing_ratio)
{
	uint32_t ratio_gap = 10;

	if ((cur_timing_info->hactive_pixels >= cur_timing_info->vactive_pixels) &&
			(pref_timing_info->hactive_pixels >= pref_timing_info->vactive_pixels) &&
			(VALUE_DIFF(cur_timing_ratio, cur_perf_timing_ratio) <= ratio_gap)) {
		dpu_pr_info("[EDID] the aspect ratio[%u] of current timing is approximately same as the current preferred ratio[%u]",
			cur_timing_ratio, cur_perf_timing_ratio);
		return true;
	}

	if ((cur_timing_info->hactive_pixels <= cur_timing_info->vactive_pixels) &&
			(pref_timing_info->hactive_pixels <= pref_timing_info->vactive_pixels) &&
			(VALUE_DIFF(cur_timing_ratio, cur_perf_timing_ratio) <= ratio_gap)) {
		dpu_pr_info("[EDID] the aspect ratio[%u] of current timing is approximately same as the current preferred ratio[%u]",
			cur_timing_ratio, cur_perf_timing_ratio);
		return true;
	}

	return false;
}

static bool edid_check_resolution_higher_than_base_value(struct edid_timing_info *cur_timing_info)
{
	uint32_t base_hactive = BASE_HACTIVE;

	if ((cur_timing_info->hactive_pixels > cur_timing_info->vactive_pixels &&
			cur_timing_info->hactive_pixels >= base_hactive) ||
			(cur_timing_info->vactive_pixels > cur_timing_info->hactive_pixels &&
			cur_timing_info->vactive_pixels >= base_hactive))
		return true;

	return false;
}

static bool edid_check_timing_resolution_higher(struct edid_timing_info *cur_timing_info,
			struct edid_timing_info *pref_timing_info, uint8_t target_screen_orientation)
{
	/* Select the higher width when the oritation of monitor is LANDSCAPE */
	if (target_screen_orientation == LANDSCAPE && cur_timing_info->hactive_pixels < pref_timing_info->hactive_pixels) {
		dpu_pr_info("[EDID] current timing[%u * %u @ %u] is lower than current preferred timing[%u * %u @ %u]",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps,
			pref_timing_info->hactive_pixels, pref_timing_info->vactive_pixels, pref_timing_info->fps);
		return false;
	}

	/* Select the higher height when the oritation of monitor is PORTRAIT */
	if (target_screen_orientation == PORTRAIT && cur_timing_info->vactive_pixels < pref_timing_info->vactive_pixels) {
		dpu_pr_info("[EDID] current timing[%u * %u @ %u] is lower than current preferred timing[%u * %u @ %u]",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps,
			pref_timing_info->hactive_pixels, pref_timing_info->vactive_pixels, pref_timing_info->fps);
		return false;
	}

	/*
	* If it can not acquire the oritation of the monitor from the EDID, select the resolution which has the higher
	* width or higher height.
	*/
	if ((cur_timing_info->hactive_pixels < pref_timing_info->hactive_pixels) ||
			(cur_timing_info->vactive_pixels < pref_timing_info->vactive_pixels)) {
		dpu_pr_info("[EDID] current timing[%u * %u @ %u] is lower than current preferred timing[%u * %u @ %u]",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps,
			pref_timing_info->hactive_pixels, pref_timing_info->vactive_pixels, pref_timing_info->fps);
		return false;
	}

	return true;
}

static bool edid_check_timing_fps_preferred(struct edid_timing_info *cur_timing_info,
            struct edid_timing_info *pref_timing_info, uint16_t target_fps)
{
	uint32_t fps_gap = 1;
	uint32_t cur_timing_ratio = 0;
	uint32_t cur_perf_timing_ratio = 0;

	/* If the resolution is same, choose the fps that is closer to the target_fps */
	if (cur_timing_info->hactive_pixels == pref_timing_info->hactive_pixels &&
			cur_timing_info->vactive_pixels == pref_timing_info->vactive_pixels &&
			(VALUE_DIFF(target_fps, cur_timing_info->fps) > VALUE_DIFF(target_fps, pref_timing_info->fps))) {
		dpu_pr_info("[EDID] current timing[%u * %u @ %u] has no more suitable fps than current preferred timing[%u * %u @ %u]",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps,
			pref_timing_info->hactive_pixels, pref_timing_info->vactive_pixels, pref_timing_info->fps);
		return false;
	}

	/* If the aspect ratio is approximately same, choose the fps that is closer to the target_fps */
	cur_timing_ratio = edid_calculate_aspect_ratio(cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels);
	cur_perf_timing_ratio = edid_calculate_aspect_ratio(pref_timing_info->hactive_pixels, pref_timing_info->vactive_pixels);
	if (edid_check_same_aspect_ratio(cur_timing_info, pref_timing_info, cur_timing_ratio, cur_perf_timing_ratio) &&
			(VALUE_DIFF(target_fps, cur_timing_info->fps) > VALUE_DIFF(target_fps, pref_timing_info->fps)) &&
			(VALUE_DIFF(cur_timing_info->fps, pref_timing_info->fps) > fps_gap)) {
		dpu_pr_info("[EDID] the fps of current timing[%u * %u @ %u] is non-preferred than current preferred timing[%u * %u @ %u]",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps,
			pref_timing_info->hactive_pixels, pref_timing_info->vactive_pixels, pref_timing_info->fps);
		return false;
	}

	return true;
}

static bool edid_check_timing_aspec_ratio_preferred(struct edid_timing_info *cur_timing_info,
    struct edid_timing_info *pref_timing_info, uint32_t target_screen_ratio, uint8_t target_screen_orientation)
{
	uint32_t cur_timing_ratio = 0;
	uint32_t cur_perf_timing_ratio = 0;

	/* If the target_screen_ratio is not set, it is would not be check the aspect ratio */
	if (target_screen_ratio <= 0 && !edid_check_timing_resolution_higher(cur_timing_info, pref_timing_info,
			target_screen_orientation)) {
		dpu_pr_info("[EDID] the screen size is not set, choose higher resolution");
		return false;
	}

	if (target_screen_ratio > 0) {
		cur_timing_ratio = edid_calculate_aspect_ratio(cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels);
		cur_perf_timing_ratio = edid_calculate_aspect_ratio(pref_timing_info->hactive_pixels, pref_timing_info->vactive_pixels);
		/* If the aspect ratios are approximately same, choose the one that has higher resolution */
		if (edid_check_same_aspect_ratio(cur_timing_info, pref_timing_info, cur_timing_ratio, cur_perf_timing_ratio) &&
				!edid_check_timing_resolution_higher(cur_timing_info, pref_timing_info, target_screen_orientation)) {
			dpu_pr_info("[EDID] Choose higher resolution when aspect ratios are approximately same");
			return false;
		}
		/*
		* Choose the preferred one that is approximately same as the target_screen_ratio.
		* If the aspect ratios of them are both not approximately same as the target screen ratio, or if it can not acquire
		* the target screen ratio from the EDID, choose the preferred one that has higher resolution.
		*/
		if (!edid_check_target_aspect_ratio(cur_timing_info, target_screen_ratio, target_screen_orientation)) {
			if (edid_check_target_aspect_ratio(pref_timing_info, target_screen_ratio, target_screen_orientation)) {
				dpu_pr_info("[EDID] current preferred ratio[%u] is same as the target ratio[%u], but current ratio[%u] not",
					cur_perf_timing_ratio, target_screen_ratio, cur_timing_ratio);
				return false;
			} else if (!edid_check_timing_resolution_higher(cur_timing_info, pref_timing_info, target_screen_orientation)) {
				dpu_pr_info("[EDID] Choose higher resolution when both are not target_screen_ratio");
				return false;
			}
		}
	}

	dpu_pr_info("[EDID] Choose current timing as preferred timing above 1080P");

	return true;
}

static bool edid_check_timing_preferred_by_product_constraint(struct edid_timing_info *cur_timing_info, int port_id)
{
	if (g_edid_hook_ops && g_edid_hook_ops->is_valid_timing_info) {
		dpu_pr_info("[EDID] g_edid_hook_ops exits!, port_id: %d", port_id);
		if (!g_edid_hook_ops->is_valid_timing_info(cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels,
				cur_timing_info->fps, (uint8_t)port_id)) {
			dpu_pr_info("[EDID] current timing[%u * %u @ %u] is not preferred for product specification",
				cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps);
			return false;
		}
	}

	return true;
}

static bool edid_preferred_timing_valid(struct edid_timing_info *edid_preferred_timing)
{
	if (edid_preferred_timing == NULL)
		return false;
	if (edid_preferred_timing->hactive_pixels <= 0 || edid_preferred_timing->vactive_pixels <= 0 ||
			edid_preferred_timing->fps <= 0)
		return false;

	return true;
}

void dpu_edid_hook_register(struct edid_hook_ops *ops)
{
	dpu_pr_info("[EDID] dpu_edid_hook_register init");
	g_edid_hook_ops = ops;
}

void edid_place_preferred_timing_at_array_first(struct edid_timing_info *timing_list,
	struct edid_timing_info **pref_timing_info, uint32_t pref_timing_id)
{
	struct edid_timing_info temp_timing_info = {0};

	if (timing_list == NULL || pref_timing_info == NULL) {
		dpu_pr_err("[EDID] timing_list or pref_timing_info is NULL");
		return;
	}

	if (pref_timing_id > 0) {
		dpu_pr_info("[EDID] Index[%u] of preferred timing is not 0, swap it with timing 0:[%u * %u @ %u]", pref_timing_id,
			timing_list[0].hactive_pixels, timing_list[0].vactive_pixels, timing_list[0].fps);
		temp_timing_info = timing_list[0];
		timing_list[0] = timing_list[pref_timing_id];
		timing_list[pref_timing_id] = temp_timing_info;
		*pref_timing_info = &timing_list[0];
	}
}

void edid_clear_invalid_timing(struct edid_timing_info *timing_list, uint32_t start_index, uint32_t end_index)
{
	uint32_t i = 0;

	dpu_pr_info("[EDID] start_index[%u], end_index[%u]", start_index, end_index);
	if (end_index > MAX_TIMING_NUM) {
		dpu_pr_warn("[EDID] end_index is invalid");
		return;
	}

	for (i = start_index; i < end_index; i++) {
		dpu_pr_info("[EDID]  Clear timing[%u]:[%u * %u @ %u]", i, timing_list[i].hactive_pixels,
			timing_list[i].vactive_pixels, timing_list[i].fps);
		memset_s(&timing_list[i], sizeof(struct edid_timing_info), 0, sizeof(struct edid_timing_info));
	}
}

int dpu_conn_edid_timing_filter(struct dkmd_connector_info *pinfo, uint8_t *edid)
{
	uint32_t i = 0;
	uint32_t cnt = 0;

	dpu_pr_info("[EDID] edid_timing_filter start");
	dpu_check_and_return(!pinfo, -EINVAL, err, "[DP] connector pinfo is nullptr!");

	if (g_edid_hook_ops && g_edid_hook_ops->save_edid) {
		if (!edid) {
			dpu_pr_warn("[EDID] save edid error, edid_t is null");
		}
		g_edid_hook_ops->save_edid(edid, DEFAULT_EDID_BUFLENGTH, (uint8_t)pinfo->port_id);
	}

	if (pinfo->base.timing_num > MAX_TIMING_NUM) {
		dpu_pr_warn("[EDID] edid_timing_num is %u, exceeds the max value", pinfo->base.timing_num);
		pinfo->base.timing_num = MAX_TIMING_NUM;
	}

	for (i = 0; i < pinfo->base.timing_num; i++) {
		/* Record the EDID preferred timing */
		if (pinfo->base.timing_list[i].preferred) {
			if (memcpy_s(&pinfo->base.edid_preferred_timing, sizeof(struct edid_timing_info), &pinfo->base.timing_list[i],
					sizeof(struct edid_timing_info)) != 0)
				dpu_pr_err("[EDID] copy edid preferred timing[%u * %u @ %u] failed", pinfo->base.timing_list[i].hactive_pixels,
					pinfo->base.timing_list[i].vactive_pixels, pinfo->base.timing_list[i].fps);
			else
				dpu_pr_info("[EDID] Timing[%u]:[%u * %u @ %u] is edid preferred timing", i, pinfo->base.timing_list[i].hactive_pixels,
					pinfo->base.timing_list[i].vactive_pixels, pinfo->base.timing_list[i].fps);
		}
		/* Check valid timing */
		if (edid_check_timing_valid(pinfo, &pinfo->base.timing_list[i]) &&
				!edid_check_same_resolution(pinfo->base.timing_list, &pinfo->base.timing_list[i], cnt)) {
			pinfo->base.timing_list[cnt++] = pinfo->base.timing_list[i];
			dpu_pr_info("[EDID] Timing[%u]:[%u * %u @ %u] conformance with specifications", i,
				pinfo->base.timing_list[i].hactive_pixels, pinfo->base.timing_list[i].vactive_pixels, pinfo->base.timing_list[i].fps);
		} else {
			dpu_pr_info("[EDID] Timing[%u]:[%u * %u @ %u] non-conformance with specifications", i,
				pinfo->base.timing_list[i].hactive_pixels, pinfo->base.timing_list[i].vactive_pixels, pinfo->base.timing_list[i].fps);
		}
	}

	if (cnt == 0) {
		dpu_pr_err("[EDID] all timings are filtered");
		return -1;
	}

	edid_clear_invalid_timing(pinfo->base.timing_list, cnt, pinfo->base.timing_num);
	edid_sort_timing_by_fps(pinfo->base.timing_list, cnt);

	pinfo->base.timing_num = cnt;
	dpu_pr_info("[EDID] Valid timing num is %u", pinfo->base.timing_num);

	for (i = 0; i < pinfo->base.timing_num; i++)
		dpu_pr_info("[EDID] Available timing[%u] is [%u * %u @ %u], pixel_clock[%llu]KHz", i,
			pinfo->base.timing_list[i].hactive_pixels, pinfo->base.timing_list[i].vactive_pixels,
			pinfo->base.timing_list[i].fps, pinfo->base.timing_list[i].pixel_clock);

	dpu_pr_info("[EDID] edid_timing_filter end");

	return 0;
}

bool edid_check_timing_preferred(struct dkmd_connector_info *pinfo, struct edid_timing_info *cur_timing_info,
	struct edid_timing_info *pref_timing_info)
{
	bool check_aspect_ratio = false;
	uint8_t target_screen_orientation = pinfo->base.screen_orientation;
	uint16_t target_fps = pinfo->base.default_target_fps;
	uint32_t target_screen_ratio = pinfo->base.screen_aspect_ratio;
	struct edid_timing_info *edid_preferred_timing = &pinfo->base.edid_preferred_timing;

	dpu_pr_debug("[EDID] EDID preferred timing is:[%u * %u @ %u]", edid_preferred_timing->hactive_pixels,
		edid_preferred_timing->vactive_pixels, edid_preferred_timing->fps);

	if (target_fps == 0) {
		target_fps = 60;
		dpu_pr_info("[EDID] target fps[%u] is invalid, set it as 60Hz", target_fps);
	}

	/* The fps of preferred timing should be same as the target fps */
	if (pinfo->is_enable_set_timing && !edid_check_supported_fps(target_fps, cur_timing_info->fps)) {
		dpu_pr_info("[EDID] current timing[%u * %u @ %u] is not preferred for fps", cur_timing_info->hactive_pixels,
			cur_timing_info->vactive_pixels, cur_timing_info->fps);
		return false;
	}

	/* Check if the timing is invalid for the product specification */
	if (!edid_check_timing_preferred_by_product_constraint(cur_timing_info, pinfo->port_id))
		return false;

	/* Preferred timing would be lower than the EDID preferred timing mode normally */
	if (edid_preferred_timing_valid(edid_preferred_timing) &&
			!edid_check_timing_resolution_higher(edid_preferred_timing, cur_timing_info, target_screen_orientation)) {
		dpu_pr_info("[EDID] Current timing[%u * %u @ %uHz] > edid preferred timing[%u * %u @ %u], can not be selected",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps,
			edid_preferred_timing->hactive_pixels, edid_preferred_timing->vactive_pixels, edid_preferred_timing->fps);
		return false;
	}

	/* If preferred timing is empty, initial it */
	if (pref_timing_info == NULL) {
		dpu_pr_info("[EDID] preferred timing is NULL, init current timing[%u * %u @ %u] as preferred timing",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps);
		return true;
	}

	/* If The resolution is higher than base value(1920 * 1080p), it should be preferred to concern with fps and aspect ratio */
	if (check_aspect_ratio && edid_check_resolution_higher_than_base_value(cur_timing_info)) {
		if (!edid_check_timing_fps_preferred(cur_timing_info, pref_timing_info, target_fps))
			return false;

		if (!edid_check_timing_aspec_ratio_preferred(cur_timing_info, pref_timing_info,
				target_screen_ratio, target_screen_orientation))
			return false;
		else
			return true;
	}

	if (!edid_check_timing_resolution_higher(cur_timing_info, pref_timing_info, target_screen_orientation))
		return false;

	dpu_pr_info("[EDID] Choose current timing as preferred timing");

	return true;
}

struct edid_timing_info *edid_get_preferred_timing(struct dkmd_connector_info *pinfo)
{
	uint32_t i = 0;
	uint32_t pref_timing_id = 0;
	struct edid_timing_info *cur_timing_info = NULL;
	struct edid_timing_info *pref_timing_info = NULL;

	dpu_pr_info("[EDID] edid_get_preferred_timing start");
	dpu_check_and_return(!pinfo, NULL, err, "[DP] connector pinfo is nullptr!");

	if (pinfo->base.timing_num > MAX_TIMING_NUM) {
		dpu_pr_warn("[EDID] edid_timing_num is %u, exceeds the max value", pinfo->base.timing_num);
		pinfo->base.timing_num = MAX_TIMING_NUM;
	}

	dpu_pr_info("[EDID] valid timing num is %u", pinfo->base.timing_num);
	for (i = 0; i < pinfo->base.timing_num; i++) {
		cur_timing_info = &pinfo->base.timing_list[i];
		dpu_pr_info("[EDID] check current timing[%u * %u @ %u] is preferred or not start",
			cur_timing_info->hactive_pixels, cur_timing_info->vactive_pixels, cur_timing_info->fps);
		if (edid_check_timing_preferred(pinfo, cur_timing_info, pref_timing_info)) {
			pref_timing_id = i;
			pref_timing_info = cur_timing_info;
			dpu_pr_info("[EDID] need update preferred timing as [%u * %u @ %u]", pref_timing_info->hactive_pixels,
				pref_timing_info->vactive_pixels, pref_timing_info->fps);
			if (pref_timing_info->preferred) {
				dpu_pr_info("[EDID] This timing is EDID preferred timing, select it");
				break;
			}
		}
	}

	if (pref_timing_info != NULL) {
		edid_place_preferred_timing_at_array_first(pinfo->base.timing_list, &pref_timing_info, pref_timing_id);
		dpu_pr_info("[EDID] Selective preferred timing is:[%u * %u @ %u]", pref_timing_info->hactive_pixels,
			pref_timing_info->vactive_pixels, pref_timing_info->fps);
	}

	dpu_pr_info("[EDID] edid_get_preferred_timing end");

	return pref_timing_info;
}
