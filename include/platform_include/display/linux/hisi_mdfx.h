/*
 * Copyright (c) 2019-2019, Hisilicon Tech. Co., Ltd. All rights reserved.
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
#ifndef DPU_MDFX_H
#define DPU_MDFX_H

#include <linux/printk.h>
#include "mdfx_uapi.h"
#include "mdfx_notify.h"

#ifdef CONFIG_MDFX_KMD
/*
 * Message printing priorities:
 * LEVEL 0 KERN_ERR (highest priority)
 * LEVEL 1 KERN_WARNING
 * LEVEL 2 KERN_NOTICE
 * LEVEL 3 KERN_INFO
 * LEVEL 4 KERN_DEBUG (Lowest priority)
 */
enum mdfx_kmsg_level {
	MDFX_LOG_LEVEL_ERR = 0x0,
	MDFX_LOG_LEVEL_WARNING,
	MDFX_LOG_LEVEL_NOTICE,
	MDFX_LOG_LEVEL_INFO,
	MDFX_LOG_LEVEL_DEBUG
};

// default event
enum mdfx_event_type {
	MDFX_EVENT_UNDERFLOW = 0,
	MDFX_EVENT_VACTIVE_TIMEOUT,
	MDFX_EVENT_FENCE_TIMEOUT,
	MDFX_EVENT_SDMA_HEBC_ERROR,
	MDFX_EVENT_NORMAL_TRIGGER,
	MDFX_EVENT_MAX
};

enum {
	DEF_EVENT_UNDER_FLOW = MDFX_EVENT_UNDERFLOW,
	DEF_EVENT_VACTIVE_TIMEOUT = MDFX_EVENT_VACTIVE_TIMEOUT,
	DEF_EVENT_FENCE_TIMEOUT = MDFX_EVENT_FENCE_TIMEOUT,
	DEF_EVENT_SDMA_HEBC_ERROR = MDFX_EVENT_SDMA_HEBC_ERROR,
	DEF_EVENT_NORMAL_TRIGGER = MDFX_EVENT_NORMAL_TRIGGER,
	DEF_EVENT_MAX
};

/*
 * traing type:
 * 0 HF (high frequency)
 * 1 SF (slow frequency)
 */
typedef enum mdfx_traing_type {
	MDFX_TRACING_TYPE_HF = 0x0,
	MDFX_TRACING_TYPE_SF,
	MDFX_TRACING_TYPE_MAX
} mdfx_traing_type_t;

#define DFX_FILEDIR         "/data/hisi_logs/graphic/"
#define DFX_LOGGER_DIR      "/data/hisi_logs/graphic/log/"
#define DFX_TRACING_DIR     "/data/hisi_logs/graphic/trace/"
#define DFX_DUMPER_DIR      "/data/hisi_logs/graphic/dump/"
#define DFX_NOTIFIER_DIR    "/data/hisi_logs/graphic/notifier/"
#define DFX_REGULATOR_DIR 	"/data/hisi_logs/graphic/notifier/regulator/"
#define DFX_DDR_DIR       	"/data/hisi_logs/graphic/notifier/ddr/"
#define DFX_LCD_KIT_DIR  	"/data/hisi_logs/graphic/notifier/lcd_kit/"

extern int32_t g_mdfx_log_level;
#define IS_ENABLE_MDFX_LOG(level) ((level) <= g_mdfx_log_level)

#define MDFX_ERR(id, msg, ...)    \
	do { \
		if (IS_ENABLE_MDFX_LOG(MDFX_LOG_LEVEL_ERR)) \
			mdfx_logger_emit(id, "[E][%s]"msg"\n", __func__, ##__VA_ARGS__); \
	} while (0)

#define MDFX_WARNING(id, msg, ...)    \
	do { \
		if (IS_ENABLE_MDFX_LOG(MDFX_LOG_LEVEL_WARNING)) \
			mdfx_logger_emit(id, "[W][%s]"msg"\n", __func__, ##__VA_ARGS__); \
	} while (0)

#define MDFX_NOTICE(id, msg, ...)    \
	do { \
		if (IS_ENABLE_MDFX_LOG(MDFX_LOG_LEVEL_NOTICE)) \
			mdfx_logger_emit(id, "[N][%s]"msg"\n", __func__, ##__VA_ARGS__); \
	} while (0)

#define MDFX_INFO(id, msg, ...)    \
	do { \
		if (IS_ENABLE_MDFX_LOG(MDFX_LOG_LEVEL_INFO)) \
			mdfx_logger_emit(id, "[I][%s]"msg"\n", __func__, ##__VA_ARGS__); \
	} while (0)

#define MDFX_DEBUG(id, msg, ...) \
	do { \
		if (IS_ENABLE_MDFX_LOG(MDFX_LOG_LEVEL_DEBUG)) \
			mdfx_logger_emit(id, "[D][%s]"msg"\n", __func__, ##__VA_ARGS__); \
	} while (0)

#define MDFX_ASSERT_IF(cond) \
	do {\
		if (cond) { \
			pr_err("[MDFX]%s,line=%d assert\n", __func__, __LINE__); \
			BUG();\
		}\
	} while (0)

/**
 * visitor_cb
 * data:composer_manager
 * value:private data
 */
typedef struct visitor_cb_param {
	int64_t id;
	uint64_t actions;
	char *even_name;
	char *timestamp;
	void *data;
	void *value;
} visitor_cb_param_t;
typedef void (*visitor_cb)(visitor_cb_param_t *param);

/*
 * Event Manager
 */
void mdfx_report_event(int64_t visitor_id, struct mdfx_event_desc *desc, void *data);
void mdfx_report_default_event(int64_t visitor_id, uint32_t default_event_type, uint32_t frame_index, void *data);

/**
 * visitor
 *
 * registor dump information callback,
 * such as dss need dump cmdlist, freq and voltage, those information just caller
 * know them.
 */
int64_t mdfx_create_visitor(uint64_t type, visitor_cb callback, void *data);
void mdfx_destroy_visitor(int64_t id);

/*
 * Tracing
 */
void mdfx_tracing_point(int64_t visitor_id, mdfx_traing_type_t tracing_type, const char *tracing_tag, char *msg);

/*
 * logger
 */
int mdfx_logger_emit(int64_t visitor_id, const char *fmt, ...);

/*
 * file
 */
struct file* mdfx_file_create_file(char *dir, char *prefix, char *postfix);
int mdfx_file_write(struct file *file, const char *buf, uint32_t buf_len);
void mdfx_remove_redundant_files(const char *dir_path);

#else  // CONFIG_MDFX_KMD

// kernel log agent
#define MDFX_EMERG(id, msg, ...)
#define MDFX_ALERT(id, msg, ...)
#define MDFX_CRIT(id, msg, ...)
#define MDFX_ERR(id, msg, ...)
#define MDFX_WARNING(id, msg, ...)
#define MDFX_NOTICE(id, msg, ...)
#define MDFX_INFO(id, msg, ...)
#define MDFX_DEBUG(id, msg, ...)
#define MDFX_ASSERT_IF(cond)

#define mdfx_create_visitor(type, callback, data)	(0)
#define mdfx_report_event(visitor_id, event_desc, data)
#define mdfx_report_default_event(visitor_id, default_event_type, frame_index, data)
#define mdfx_destroy_visitor(id)
#define mdfx_tracing_point(visitor_id, tracing_type, tracing_tag, msg)
#define mdfx_logger_emit(visitor_id, fmt, ...)

#endif

#endif /* MDFX_H */
