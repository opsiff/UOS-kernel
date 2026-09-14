/**
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

#ifndef DPU_COMP_MULTI_PRESENT_H
#define DPU_COMP_MULTI_PRESENT_H
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/ktime.h>

// The value must be the same as that defined on the dacc.
enum PRESENT_CLIENT_TYPE {
    PRESENT_CLIENT_MAIN,
    PRESENT_CLIENT_TUNNEL,
    PRESENT_CLIENT_SELF_REFRESH,    // Always last client type
    PRESENT_CLIENT_TYPE_MAX
};

struct present_client {
    bool has_present_request;
    bool need_execute_present;
};
struct dpu_comp_dfr_ctrl;
struct dpu_multi_present_ctrl;

struct multi_present_ops {
	/**
	* @brief:  multi_present_ctrl initialization
	* @param:  multi_present_ctrl:struct dpu_multi_present_ctrl *
	* @return: void
	*/
	void (*init_param)(struct dpu_multi_present_ctrl *multi_present_ctrl);

	/**
	* @brief:  present commit request, set has_present_request flag
	* @param:  multi_present_ctrl:struct dpu_multi_present_ctrl *
	* @param:  client_type: present commit type value in enum PRESENT_CLIENT_TYPE
	* @return: void
	*/
	void (*set_present_request)(struct dpu_multi_present_ctrl *multi_present_ctrl, uint32_t client_type);

	/**
	* @brief:  after sfu and ldi, config sfu to vactivce start state param, only acpu used
	* @param:  multi_present_ctrl:struct dpu_multi_present_ctrl *
	* @return: void
	*/
	void (*set_sfu2vactive_param)(struct dpu_multi_present_ctrl *multi_present_ctrl);

	/**
	* @brief:  need congfig sfu and ldi or not, only acpu used
	* @param:  multi_present_ctrl:struct dpu_multi_present_ctrl *
	* @return: true: need config sfu and ldi, false: not need
	*/
	bool (*need_config_sfu)(struct dpu_multi_present_ctrl *multi_present_ctrl);

	/**
	* @brief:  vactive start notify clear sfu2vactive param
	* @param:  dfr_ctrl:struct dpu_comp_dfr_ctrl *
	* @return: void
	*/
	void (*clear_sfu2vactive_param)(struct dpu_comp_dfr_ctrl *dfr_ctrl);

	/**
	* @brief:  this time is safe period or unsafe period
	* @param:  dfr_ctrl:struct dpu_comp_dfr_ctrl *
	* @return: true: safe period, false: unsafe period
	*/
	bool (*is_safe_period)(struct dpu_comp_dfr_ctrl *dfr_ctrl);

	/**
	* @brief:  set present config period state
	* @param:  dfr_ctrl:struct dpu_comp_dfr_ctrl *
	* @param:  flag: true: begin config, false: end config
	* @return: void
	*/
	void (*set_present_config_period)(struct dpu_comp_dfr_ctrl *dfr_ctrl, bool flag);

	/**
	* @brief:  get client present is in sfu to vactive start state
	* @param:  dfr_ctrl:struct dpu_comp_dfr_ctrl *
	* @param:  client_type: present type value in enum PRESENT_CLIENT_TYPE
	* @return: true: client_type is in sfu to vactive start period, false: not
	*/
	bool (*in_sfu2vactive_period)(struct dpu_comp_dfr_ctrl *dfr_ctrl, uint32_t client_type);

	/**
	* @brief:  wake up wait vactive start event
	* @param:  present:struct comp_online_present *
	* @return: void
	*/
	void (*wake_up_vactive_wait_event)(struct comp_online_present *present);

	/**
	* @brief:  dump multi present info
	* @param:  dfr_ctrl:struct dpu_comp_dfr_ctrl *
	* @return: void
	*/
	void (*dump_info)(struct dpu_comp_dfr_ctrl *dfr_ctrl);
};

struct dpu_multi_present_ctrl {
	ktime_t last_vsync_timestamp;
	bool is_sfu2vactive_period;
	bool is_present_config_period;
    struct present_client clients[PRESENT_CLIENT_TYPE_MAX];
    struct multi_present_ops *ops;
};

void dpu_multi_present_init(struct dpu_multi_present_ctrl *multi_present_ctrl, int32_t dfr_mode);
void dpu_multi_present_register_ops_dfr_by_acpu(struct dpu_multi_present_ctrl *multi_present_ctrl);
void dpu_multi_present_register_ops_dfr_by_mcu(struct dpu_multi_present_ctrl *multi_present_ctrl);

#endif
