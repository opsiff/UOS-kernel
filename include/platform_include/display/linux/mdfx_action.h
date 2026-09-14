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
#ifndef MDFX_ACTION_H
#define MDFX_ACTION_H

#ifdef _BIT_
#undef _BIT_
#endif

#define _BIT_(x)   (1u << (x))
#define BIT64(x)   (1uLL << (x))

/* visitor type */
enum _visitor_type {
	VISITOR_TYPE_HDM = 0,
	VISITOR_TYPE_HAE = 1,

	VISITOR_TYPE_GRAPHIC_MAX = 7,
	VISITOR_TYPE_OTHER = 8,
	VISITOR_TYPE_MAX
};

enum visitor_type {
	VISITOR_GRAPHIC_HDM  = BIT64(VISITOR_TYPE_HDM),
	VISITOR_GRAPHIC_HAE  = BIT64(VISITOR_TYPE_HAE),
	VISITOR_GRAPHIC_MASK = 0xFFuLL,
};

#define TRACING_DETAIL_MAX   24
/**
 * dump：提供公共信息的dump，例如cpu调用栈等，其他领域私有信息，利用Nofity回调执行
 */
enum action_detail {
	// dump: 16个bit， 0~15
	DUMP_DETAIL_IMAGE_BUF  = BIT64(0),
	DUMP_DETAIL_LAYER_INFO = BIT64(1),
	DUMP_DETAIL_ALL_CPU_RUNNABLE = BIT64(2),
	DUMP_DETAIL_USER_CALLSTACK = BIT64(3),
	DUMP_DETAIL_DACC_LOG = BIT64(4),
	DUMP_DETAIL_MASK = 0xFFFFuLL,

	// log: 8个bit 16~23
	LOG_DETAIL_STORE_USER_LOG  = BIT64(16),
	LOG_DETAIL_STORE_KMD_LOG   = BIT64(17),
	LOG_DETAIL_MASK = 0xFFuLL << 16,

	// notify 8个bit 24~31
	NOTIFY_DETAIL_DUMP_CMDLIST = BIT64(24),
	NOTIFY_DETAIL_DDR_INFO = BIT64(25),
	NOTIFY_DETAIL_MASK = 0xFFuLL << 24,
	NOTIFY_DETAIL_ALL_INFO = NOTIFY_DETAIL_MASK,

	// tracing 24个bit 32~55， 每个bit的具体含义，由各个visitor定义
	TRACING_DETAIL_0 = BIT64(32),
	TRACING_DETAIL_1 = BIT64(33),
	TRACING_DETAIL_2 = BIT64(34),
	TRACING_DETAIL_3 = BIT64(35),
	TRACING_DETAIL_4 = BIT64(36),
	TRACING_DETAIL_MASK = 0xFFFFFFuLL << 32,

	// 预留 8个bit
	RESERVED_DETAIL_0          = BIT64(40),
};

#define NEED_REPORT_TO_USER_MODE_ACTIONS  (DUMP_DETAIL_IMAGE_BUF | DUMP_DETAIL_LAYER_INFO | \
                                           LOG_DETAIL_STORE_USER_LOG)

#endif