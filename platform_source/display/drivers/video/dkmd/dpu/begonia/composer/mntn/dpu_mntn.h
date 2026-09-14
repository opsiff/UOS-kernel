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
#ifndef _DPU_MNTN_H_
#define _DPU_MNTN_H_

#ifdef CONFIG_MDFX_KMD
#include <linux/fs.h>
#include <dpu/soc_dpu_define.h>
#include "hisi_mdfx.h"
#include "dpu_cmdlist.h"

///// struct define /////
#define DPU_MNTN_DUMP_FRAME_NUMBER 2

struct dpu_mntn_dump_ctrl {
    struct dpu_mntn_reg_module *modules;
    struct dpu_cmdlist_frame_info frames[DPU_MNTN_DUMP_FRAME_NUMBER];
};

extern int64_t g_dkmd_mdfx_id;
extern uint32_t g_mdfx_caps;

void dkmd_create_mdfx_client(void *data);
void dkmd_destroy_mdfx_client(void);
void dpu_mntn_report_event(uint32_t event_type, const struct comp_online_present *present);

#define mdfx_trace_begin(tag, extra_param) \
	do { \
		if (MDFX_HAS_CAPABILITY(g_mdfx_caps, MDFX_CAP_TRACING)) \
			mdfx_tracing_point(g_dkmd_mdfx_id, MDFX_TRACING_TYPE_HF, tag, #extra_param " begin"); \
	} while (0)

#define mdfx_trace_end(tag, extra_param) \
	do { \
		if (MDFX_HAS_CAPABILITY(g_mdfx_caps, MDFX_CAP_TRACING)) \
			mdfx_tracing_point(g_dkmd_mdfx_id, MDFX_TRACING_TYPE_HF, tag, #extra_param " end"); \
	} while (0)

#define mdfx_trace_point(tag, msg) \
	do { \
		if (MDFX_HAS_CAPABILITY(g_mdfx_caps, MDFX_CAP_TRACING)) \
			mdfx_tracing_point(g_dkmd_mdfx_id, MDFX_TRACING_TYPE_HF, tag, msg); \
	} while (0)

#define mdfx_trace_key(tag, msg) \
	do { \
		if (MDFX_HAS_CAPABILITY(g_mdfx_caps, MDFX_CAP_TRACING)) \
			mdfx_tracing_point(g_dkmd_mdfx_id, MDFX_TRACING_TYPE_SF, tag, msg); \
	} while (0)
#else
#define dkmd_create_mdfx_client(data)
#define dkmd_destroy_mdfx_client()
#define dpu_mntn_report_event(event_type, present)

#define mdfx_trace_begin(tag, extra_param)
#define mdfx_trace_end(tag, extra_param)
#define mdfx_trace_point(tag, msg)
#define mdfx_trace_key(tag, msg)

#endif // #ifdef CONFIG_MDFX_KMD

#endif
