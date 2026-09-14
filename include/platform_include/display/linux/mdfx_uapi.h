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
#ifndef __UAPI_LINUX_DPU_MDFX_H
#define __UAPI_LINUX_DPU_MDFX_H

#include <linux/types.h>
#include "mdfx_action.h"

#define IS_ENABLE_BITS_FIELD(value, bitsfield) (((value) & (bitsfield)) == (bitsfield))
#define DISABLE_BITS_FIELD(value, bitsfield) ((value) &= ~(bitsfield))

#define ENABLE_BIT(value, bit)   ((value) & _BIT_(bit))
#define ENABLE_BIT64(value, bit) ((value) & BIT64(bit))
#define DISABLE_BIT64(value, bit) ((value) &= ~(BIT64(bit)))

#define DPU_MDFX_IOCTL_MAGIC 'D'
/*
 * user mode service query the log files specify,
 * such as the max buf size, the max log files nums
 */
#define MDFX_QUERY_FILE_SPEC _IOW(DPU_MDFX_IOCTL_MAGIC, 0x01, struct mdfx_file_spec)
#define MDFX_DMD_REPORT      _IOW(DPU_MDFX_IOCTL_MAGIC, 0x05, unsigned int)

#define MDFX_DELIVER_EVENT   _IOW(DPU_MDFX_IOCTL_MAGIC, 0x09, struct mdfx_event)
#define MDFX_DUMP_INFO       _IOW(DPU_MDFX_IOCTL_MAGIC, 0x0a, struct mdfx_dump_desc)
#define MDFX_TRACE_INFO      _IOW(DPU_MDFX_IOCTL_MAGIC, 0x0b, struct mdfx_tracing_desc)
#define MDFX_ADD_VISITOR     _IOW(DPU_MDFX_IOCTL_MAGIC, 0x0c, struct mdfx_visitor_desc)
#define MDFX_REMOVE_VISITOR  _IOW(DPU_MDFX_IOCTL_MAGIC, 0x0d, int64_t)
#define MDFX_GET_CAPABILITY  _IOW(DPU_MDFX_IOCTL_MAGIC, 0x0e, uint32_t)
#define MDFX_SET_CAPABILITY  _IOW(DPU_MDFX_IOCTL_MAGIC, 0x0f, uint32_t)

#define MDFX_EVENT_NAME_MAX  32
#define MDFX_EVENT_TIMESTAMP_MAX  32

/* this define must be equal to enum::EventActor */
enum mdfx_actor_type {
	ACTOR_DUMPER = 0,
	ACTOR_TRACING,
	ACTOR_LOGGER,
	ACTOR_NOTIFIER,
	ACTOR_MAX,
};

enum mdfx_capability {
	MDFX_CAP_NULL = 0,
	MDFX_CAP_LOGGER = 1 << 0,
	MDFX_CAP_DUMPER = 1 << 1,
	MDFX_CAP_SAVER =  1 << 2,
	MDFX_CAP_TRACING = 1 << 3,
	MDFX_CAP_EVENT = 1 << 4,
};

#define MDFX_HAS_CAPABILITY(caps, needCaps) (!!((caps) & (needCaps)))

/* action type */
enum action_type {
	ACTION_DUMP_INFO     = BIT64(ACTOR_DUMPER),
	ACTION_TRACE_RECODE  = BIT64(ACTOR_TRACING),
	ACTION_LOG_PRINT     = BIT64(ACTOR_LOGGER),
	ACTION_NOTIFY        = BIT64(ACTOR_NOTIFIER),
};

struct mdfx_event_desc {
	char event_name[MDFX_EVENT_NAME_MAX];
	uint32_t event_type;
	uint32_t frame_index;
	uint64_t relevant_visitor_actions[VISITOR_TYPE_MAX];
	uint64_t current_visitor_actions;
};

struct mdfx_event {
	int64_t visitor_id;
	struct mdfx_event_desc desc;
	char timestamp[MDFX_EVENT_TIMESTAMP_MAX];
};

struct mdfx_dump_desc {
	int64_t id;
	uint64_t dump_infos;
};

#define TRACING_POINT_MSG_MAX_LEN 20
#define TRACING_TYPE_NAME_MSX_LEN 10

struct mdfx_tracing_desc {
	int64_t id;
	char type_name[TRACING_TYPE_NAME_MSX_LEN];
	uint32_t msg_len;
	char msg[TRACING_POINT_MSG_MAX_LEN];
};

/*
 * id is a num, which it was init by mdfx service at user mode,
 * if id is -1, mdfx driver will initial it.
 * type is a enmu value which can be initial by enum visitor_type
 * pid is client's pid
 */
struct mdfx_visitor_desc {
	int64_t id;
	uint64_t type;
	int64_t pid;
};

// file generate
struct mdfx_file_spec {
	uint32_t file_max_size;
	uint32_t file_max_num;
};

#endif /* __UAPI_LINUX_DPU_MDFX_H */
