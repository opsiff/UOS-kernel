/*
 * Copyright (c) 2025-2025, Hisilicon Tech. Co., Ltd. All rights reserved.
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
#ifndef MDFX_NOTIFY_H
#define MDFX_NOTIFY_H

#include <linux/types.h>
#include <linux/notifier.h>
#include "mdfx_action.h"
#include "mdfx_uapi.h"

#ifdef CONFIG_MDFX_KMD
struct mdfx_event_value {
	char event_name[MDFX_EVENT_NAME_MAX];
	int64_t visitor_id;
	uint64_t actions; // action_detail
};

// visitor_type: means which type will notify this event
int mdfx_register_client(struct notifier_block *nb, uint64_t visitor_type);
int mdfx_unregister_client(struct notifier_block *nb, uint64_t visitor_type);
#else
#define mdfx_register_client(nb, visitor_type)
#define mdfx_unregister_client(nb, visitor_type)
#endif

#endif