/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description: jpegd drv.
 * Create: 2023-10-31
 */

#ifndef JPU_POWER_MANAGER_H
#define JPU_POWER_MANAGER_H

#include "jpu.h"
#include "jpu_timer.h"

int32_t jpu_on(struct jpu_data_type *jpu_device);
int32_t jpu_off(struct jpu_data_type *jpu_device);
int32_t jpu_pre_off(struct jpu_data_type *jpu_device);
int32_t jpu_aft_on(struct jpu_data_type *jpu_device);
void update_time(struct jpu_data_type *jpu_device, uint64_t time);
void trigger_power_off(struct jpu_data_type *jpu_device, uint64_t time, uint32_t cond);
#endif
