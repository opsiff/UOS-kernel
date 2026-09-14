/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2023. All rights reserved.
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

#ifndef COMPOSER_DFR_H
#define COMPOSER_DFR_H

#include <dkmd_dpu.h>
#include <linux/kthread.h>
#include "dkmd_object.h"
#include "dkmd_dfr_info.h"
#include "online/dpu_comp_vsync.h"
#include "ppu/dpu_comp_ppu.h"

struct comp_online_present;

#define DDIC_CMDS_MAX_LEN 10
/* dynamic frame rate control struct, which include dfr runtime info and
 * abstraction itf for different dfr mode */

typedef union
{
    uint32_t value;
    struct
    {
        uint32_t ddic_type                            : 2;  // bit0-1
        uint32_t self_refresh_rate                    : 8;  // bit2-9
        uint32_t need_auxiliary_self_refresh          : 1;  // bit10
        uint32_t enable_multi_present                 : 1;  // bit11
        uint32_t safe_frm_disable                     : 1;  // bit12
        uint32_t is_refresh_rate_reseted              : 1;  // bit13
        uint32_t need_auxiliary_self_refresh_aod      : 1;  // bit14
        uint32_t oled_type                            : 1;  // bit15
        uint32_t ltpo_ver                             : 1;  // bit16
        uint32_t is_switch_te_delay                   : 1;  // bit17
        uint32_t reserved                             : 14;
    } param;
} dfr_init_config_union;

struct dfr_ctrl_ops {
	void (*setup_data)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*release_data)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*set_dvfs_level)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*restore_dvfs_level)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*power_off_sub)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	int32_t (*switch_frm_rate)(struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t frame_rate);
	int32_t (*commit)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	int32_t (*send_dcs_cmds_with_frm)(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct disp_effect_params *effect_params);
	int32_t (*send_dcs_cmds_with_refresh)(struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t bl_level);
	int32_t (*update_frm_rate_isr_handler)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	int32_t (*set_safe_frm_rate)(struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t safe_frm_rate);
	int32_t (*send_safe_frm_rate)(struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t safe_frm_rate);
	void (*send_refresh)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*enable_dimming)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*disable_dimming)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*enable_safe_frm_rate)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*disable_safe_frm_rate)(struct dpu_comp_dfr_ctrl *dfr_ctrl, int32_t disable_type);
	void (*exit_idle_status)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*enter_idle_event)(struct dpu_vsync *vsync_ctrl);
	int32_t (*setup_irq)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*release_irq)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	void (*timeout_print)(struct dpu_composer *dpu_comp, struct comp_online_present *present);
	bool (*need_wait_again)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
	bool (*is_fisrt_frame)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
};

struct dpu_comp_dfr_ctrl {
	int32_t mode;
	uint32_t pre_te_rate;
	uint32_t cur_te_rate;
	uint32_t next_te_rate;
	uint32_t pre_te_mask_num;
	uint32_t cur_te_mask_num;
	uint32_t pre_frm_rate;
	uint32_t cur_frm_rate;
	uint32_t next_frm_rate;
	uint32_t active_frm_rate;
	uint32_t cur_safe_frm_rate;
	uint32_t next_te_level;
	uint32_t next_frm_level;
	uint32_t next_safe_frm_rate;
	uint32_t next_safe_frm_rate_level;
	uint32_t present_buffer_num;
	bool resend_switch_cmds;
	bool follow_frame;
	void* priv_data;
	struct dpu_composer *dpu_comp;
	struct kthread_work dfr_work;

	uint32_t fps_sup_num;
	uint32_t fps_sup_seq[FPS_LEVEL_MAX];
	uint64_t frame_id;
	uint64_t request_time_manos;
	uint32_t time_nanos_type;

	uint32_t porch_fps;
	struct work_struct dfr_dvfs_notice_work;
	struct workqueue_struct *dfr_dvfs_notice_wq;

	/* maximum offset for vsync on different platforms(according to count TE) */
	uint32_t vsync_offset_threshold;
	uint32_t unsafe_period;

	struct dpu_comp_ppu_ctrl ppu_ctrl;

	struct dfr_ctrl_ops *ops;
};

void dpu_dfr_dvfs_notice_handle_func(struct work_struct *work);
void dpu_dfr_dvfs_notice_register(struct dpu_comp_dfr_ctrl *dfr_ctrl);
void dpu_dfr_dvfs_notice_unregister(struct dpu_comp_dfr_ctrl *dfr_ctrl);

void dpu_comp_dfr_ctrl_setup(struct dpu_composer *dpu_comp, struct comp_online_present *present);
void dpu_comp_dfr_ctrl_process(struct dpu_composer *dpu_comp, struct comp_online_present *present,
	struct disp_frame *frame);

int32_t dkmd_dfr_send_refresh(uint32_t conn_id, uint32_t panel_type);
int32_t dkmd_dfr_send_safe_frm_rate(uint32_t conn_id, uint32_t panel_type, uint32_t safe_frm_rate, const struct sfr_info *sfr_info);
int32_t dkmd_dfr_enable_dimming(uint32_t conn_id, uint32_t panel_type);
int32_t dkmd_dfr_disable_dimming(uint32_t conn_id, uint32_t panel_type);
int32_t dkmd_dfr_enable_safe_frm_rate(uint32_t conn_id, uint32_t panel_type);
int32_t dkmd_dfr_disable_safe_frm_rate(uint32_t conn_id, uint32_t panel_type, int32_t disable_type);

#endif

