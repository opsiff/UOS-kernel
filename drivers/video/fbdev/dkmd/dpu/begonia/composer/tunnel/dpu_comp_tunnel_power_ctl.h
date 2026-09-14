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
#ifndef DPU_COMP_TUNNEL_POWER_CTL_H
#define DPU_COMP_TUNNEL_POWER_CTL_H

#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/semaphore.h>
#include "dkmd_dpu.h"

#ifdef CONFIG_DKMD_DPU_TUNNEL_DEVICE_PRESENT
struct dpu_composer;
void dpu_tunnel_hwspinlock_init(void);
int32_t dpu_tunnel_proc_enter_idle(struct dpu_composer *dpu_comp);
void dpu_tunnel_proc_exit_idle(uint32_t dev_commit_layer_cnt);
void dpu_tunnel_proc_power_on(struct dpu_composer *dpu_comp);
void dpu_tunnel_proc_power_off(struct dpu_composer *dpu_comp);
#else
#define dpu_tunnel_hwspinlock_init()
#define dpu_tunnel_proc_enter_idle(dpu_comp) (0)
#define dpu_tunnel_proc_exit_idle(dev_commit_layer_cnt)
#define dpu_tunnel_proc_power_on(dpu_comp)
#define dpu_tunnel_proc_power_off(dpu_comp)
#endif
#endif
