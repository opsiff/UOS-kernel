/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2012-2025. All rights reserved.
 *
 * jpeg jpu timer
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"

#include "jpu_timer.h"

#define NSEC_PER_SEC 1000000000L
#define NSEC_PER_MSEC 1000000L

uint64_t jpu_get_msec(void)
{
    uint64_t milliSec;
    uint64_t nanoSec;
    struct timespec64 ts;

    ktime_get_ts64(&ts);
    nanoSec = ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
    milliSec = nanoSec / NSEC_PER_MSEC;

    return milliSec;
}

#pragma GCC diagnostic pop