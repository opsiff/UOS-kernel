/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
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

#ifndef DKMD_MIPI_DSI_H
#define DKMD_MIPI_DSI_H

#include "dkmd_mipi_panel_info.h"
#include "dkmd_cmds_info.h"
#include <dpu/soc_dpu_define.h>

#define DSI_CMD_HDR_LEN 4
/* Default async tx task max num is 6 */
#ifndef DPU_ASYNC_TX_TASK_MAX_TX_NUM
#define DPU_ASYNC_TX_TASK_MAX_TX_NUM 6
#endif
#ifndef MIPI_ASYNC_HDR_FIFO_LEN
#define MIPI_ASYNC_HDR_FIFO_LEN 64
#endif
/* Async cmdlist support nums (support up to 32) */
#define MIPI_ASYNC_TX_MAX_NUM DPU_ASYNC_TX_TASK_MAX_TX_NUM
#define MIPI_ASYNC_PAYLOAD_FIFO_LEN 640 /* payload fifo bytes */
#define DSI_HP_FIFO_CMDS_MAX (MIPI_ASYNC_HDR_FIFO_LEN / DSI_CMD_HDR_LEN)
#define MIPI_CHECK_CMDS_FORBID_TIME_PERIOD 500 /* us */
#define MIPI_ASYNC_CMDS_TIME_MAX 580 /* us */
#define MIPI_SYNC_ASYNC_CUSHION_TIME 1000 /* us */
#define MIPI_DDIC_READ_BACK_TIMEOUT 35000 /* us */
#define MIPI_DSI_READ_CHECK_WAIT 50  /* us */
#define MIPI_CHECK_WINDOW_TIME_PERIOD 100  /* us */
#define DSI_GROUP_CMDS_NUM_MAX 50
#define MIPI_SYNC_WIN_TIME_IN_ADAPT_VSYNC 1000 /* us */
#define MIPI_GROUP_RX_TIMEOUT 500000 /* us */

/*
 * What is ASYNC transmission mode?
 * ASYNC mode is that platform collect cmds and transmit cmds at different time region,
 * Interface of mipi_dsi_tx is just used for collecting tx cmds.
 * It use the time region that between timer isr to te isr(usually 700us), to transmit cmds,
 * It use the other time region to collect cmds, please refer to the below diagram.
 * The major function is that it can avoid tx cmds across frame.
 *
 * timer isr:       capture    capture    capture                          capture    capture    capture    capture
 *                    |          |          |                                |          |          |          |
 * te isr   :|----------|----------|----------|--------------------------------|----------|----------|----------|
 *           te         te         te         te                               te         te         te         te
 * dsi state:|-------------idle-------------/-\------------vactive-----------/-\-------------idle---------------|
 *                                   |vsw|vbp|wait te|                      |vfp|
 *
 * When do you must use ASYNC transmission mode?
 * If LTPO Panel, you must use async mode to transmit cmds, which use DMCU to ctrl frame rate.
 * If LTPS Panel, you must use sync mode.
 * Other cases, you must use sync mode, power on/off, esd, rx, product line, ACPU ctrl frame rate, etc
 *
 * What limitations of ASYNC transmission mode?
 * (1) Only support tx cmds, not support rx cmds.
 * (2) It use 1 timer clock(31.25us) to transmit once, max cmds num of one transmission, refers to the FIFO size,
 *     (WARNING that if the cmds overflow, they will be dropped).
 * (3) One transmission will use one cmdlist, which corresponds to one collection,
 *	   for example, one call of mipi_dsi_tx, or one present with some cmds.
 *     We can support 6 transmissions(6*62.5us=375us) for one capture.
 * (4) The cmds that need to be transmitted before presentation, owns high priority.
 * (5) ESD checking must avoid the transmission region,
 *	   we may use the HW-VSYNC(which less than 120HZ FPS) to send rx/tx esd cmds.
 *
 * 8425L Platform DSI FIFO size limitation is as below:
 * Low priority header: 16 (64 bytes)
 * Low priority payloads: 160 bytes
 * High priority header: 16 (64 bytes)
 * High priority payloads: 640 bytes
 */
#define DSI_CMDS_NUM 32

enum mipi_dsi_id {
	MIPI_DSI_ID_0 = 0,
	MIPI_DSI_ID_1,
	MIPI_DSI_ID_2,
	MIPI_DSI_ID_MAX,
};

/*
 * DSI supports both LP(Low Power, 10Mbps) and HS(High Speed, 80Mbps ~ 1.5Gbps) mode.
 * When DSI is in HS mode and transmitting image, it cannot switch to LP mode, and cannot transmit cmds.
 */
enum mipi_dsi_power_mode {
	MIPI_DSI_POWER_MODE_LP,
	MIPI_DSI_POWER_MODE_HS,
	MIPI_DSI_POWER_MODE_SINGLE,
	MIPI_DSI_POWER_MODE_MAX,
};

enum mipi_dsi_trans_mode {
	MIPI_DSI_MODE_TRANS_SYNC,
	MIPI_DSI_MODE_TRANS_ASYNC,
	MIPI_DSI_MODE_MAX,
};

enum mipi_dsi_err {
	MIPI_E_UNKNOWN = 1,
	MIPI_E_ALLOC_FAILED,
	MIPI_E_NULL_PTR,
	MIPI_E_INVALID_PANEL,
	MIPI_E_ASYNC_NOT_SUPPORT,
	MIPI_E_TOO_MANY_CMDS,
	MIPI_E_TOO_MANY_TXS,
	MIPI_E_LP_NOT_SUPPORT,
	MIPI_E_TIMEOUT,
	MIPI_E_ASYNC_STOPED,
	MIPI_E_REPLACE_TASK_FAILED,
	MIPI_E_INVALID_PARAMS,
};

enum mipi_dsi_group_rx_err {
	MIPI_E_PARAMS_INVALID = 1,
	MIPI_E_POWER_OFF,
	MIPI_E_ACTIVE_VSYNC_FAILED,
	MIPI_E_READ_TOTAL_TIMEOUT,
	MIPI_E_CMDS_SEND_FAILED,
	MIPI_E_WAIT_WINDOW_TIMEOUT,
	MIPI_E_FIFO_FULL,
	MIPI_E_WRITE_FAILED,
	MIPI_E_READ_FAILED,
	MIPI_E_INTERFACE_NOT_SUPPORT,
};

struct mipi_dsi_out {
	uint8_t *out;
	int32_t out_len;
};

/* mipi_dsi_tx_params power mode and delay detail:
 *
 * cmdlist trigger:                                       cmdlist1     cmdlist2
 *                                                           |-send-|    |-send-|
 * timing   :|--------------------------------|-----------------------------------|
 *           te                            capture                                te
 * delay    :                                                |---delay---|
 *
 * delay is the time between two triggers, DMCU can delay times of timer clock(31.25us).
 * total delay thresholds is 450us, if exceeded threshold, cmdlist will be droped.
 * as time between capture and te is 770us.
 * sync cmds tx time is 312.5us = 10 * 31.25 (it can adjust according to the actual situation)
 * sync cmds tx time is 457.5us = 770 - 312.5
 *
 * for different power mode:
 * LP power mode is for debug, we fix delay between two cmdlists  125us = 4 * 31.25.
 * HS power mode default delay is 1 timer clock (31.25).
 * if you want larger delay, you can set delay in last cmdlist cmd.
 * the delay will round up to times of timer clock (31.25us).
 * if set delay 94us, 94/31.25 = 3.008, it will round up to 31.25 * 4 = 125us.
 *
 * important:
 * 1. those sync interface can not be mixed with the old sync interface defined in dkmd_lcd_interface.h
 */
struct mipi_dsi_tx_params {
	/* cmd or video mode, single or dual-mipi, online or offline, primary or external */
	uint32_t panel_type;
	enum mipi_dsi_id dsi_id;
	enum dkmd_cmds_type cmds_type;
	enum mipi_dsi_trans_mode trans_mode;
	enum mipi_dsi_power_mode power_mode;

	struct mipi_dsi_cmds write_cmds;
	/* SYNC mode only, caller need to confirm that dss is active */
	bool is_isr_safe; // reserved
	bool has_refresh;

	// not open for lcdkit
	bool follow_frame;
	bool is_force_sync;
	uint32_t validate_repetition;
	/* hardware wait: us */
	uint32_t hardware_wait;
};

struct mipi_dsi_rx_params {
	/* cmd or video mode, single or dual-mipi, online or offline, primary or external */
	uint32_t panel_type;
	enum mipi_dsi_id dsi_id;
	enum mipi_dsi_power_mode power_mode;

	bool is_little_endian;
	struct dsi_cmd_desc read_cmd;
	struct mipi_dsi_out read_outs;
};

struct mipi_dsi_group_rx_params {
	uint32_t panel_type;
	enum mipi_dsi_id dsi_id;
	enum mipi_dsi_power_mode power_mode;
	bool is_little_endian;

	struct mipi_dsi_cmds read_cmds;
	struct mipi_dsi_out read_outs;
};

struct mipi_dsi_connector_params {
	uint32_t panel_type;
	enum mipi_dsi_id dsi_id;
};

/** tx==transmit,
 * support multiple write request cmds
 */
int32_t dpu_mipi_dsi_tx(struct mipi_dsi_tx_params *params);

/** rx==receive,
 * support only one read request cmd, and sync mode
 * need more time to wait ack(lp mode), during waiting, read/write cmds cannot be invoked
 */
int32_t dpu_mipi_dsi_rx(struct mipi_dsi_rx_params *params);

/** rx==receive,
 * support multiple read request cmds, and sync mode
 * need more time to wait ack(lp mode), during waiting, read/write cmds cannot be invoked
 */
int32_t dpu_mipi_dsi_group_rx(struct mipi_dsi_group_rx_params *params);

int32_t dpu_dual_mipi_dsi_tx(struct mipi_dsi_tx_params *dsi0_params, struct mipi_dsi_tx_params *dsi1_params);

/* dual mipi rx only support same cmd for two dsi
 * use dsi0 : power_mode, is_little_endian, read_cmd, read_outs.out_len
 * return: dsi0_params->read_outs.out, dsi1_params->read_outs.out
 */
int32_t dpu_dual_mipi_dsi_rx(struct mipi_dsi_rx_params *dsi0_params, struct mipi_dsi_rx_params *dsi1_params);

int32_t dpu_mipi_dsi_async_tx_stop(struct mipi_dsi_connector_params *con_params);

int32_t dpu_dual_mipi_dsi_group_rx(struct mipi_dsi_group_rx_params *dsi0_params, struct mipi_dsi_group_rx_params *dsi1_params);

#endif
