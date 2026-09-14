/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
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
#include "utils/comm_utils.h"
#include "dkmd_log.h"
#include "dp_ctrl_common.h"
#include "dkmd_connector.h"

#define MAX_UEVENT_BUFFER_LENGTH 120
#define DELAY_MARGIN 2
#define MAX_WAIT_TIME 100
#define DEFAULT_WAIT_TIME 35

static struct workqueue_struct *g_dptx_all_event_handle_wq = NULL;

void dptx_create_workqueue(void)
{
	if (g_dptx_all_event_handle_wq == NULL) {
		g_dptx_all_event_handle_wq = create_singlethread_workqueue("dptx_all_event_handle_wq");
		if (g_dptx_all_event_handle_wq == NULL)
			dpu_pr_err("[DP] create dptx_all_event_handle_wq failed");
	}
	return;
}
void dptx_destory_workqueue(void)
{
	if (g_dptx_all_event_handle_wq != NULL) {
		destroy_workqueue(g_dptx_all_event_handle_wq);
		g_dptx_all_event_handle_wq = NULL;
	}
}

void dptx_work_queue_handle(struct dp_ctrl *dptx, enum dptx_work_queue_event event_type)
{
	dpu_check_and_no_retval(!dptx || !g_dptx_all_event_handle_wq,  err, "[DP] NULL Pointer");
	dpu_pr_debug("[DP] dptx_work_queue_handle event type %u, port id is %d", event_type, dptx->port_id);
	switch (event_type) {
	case DPTX_GPIO_PLUG_EVENT:
		queue_work(g_dptx_all_event_handle_wq, &dptx->dptx_pluggable_work);
		break;
	case DPTX_PSR2_UEVENT_EVENT:
		queue_work(g_dptx_all_event_handle_wq, &dptx->dptx_psr2_uevent_work);
		break;
	case DPTX_MAINTENANCE_CHECK_SYMBOL_ERROR:
		queue_work(g_dptx_all_event_handle_wq, &dptx->dptx_maintenance_check_ber_work);
		break;
	default:
		dpu_pr_err("[DP] dptx_work_queue_handle event type %u is error", event_type);
		break;
	}
}
void switch_notification_event_unchange_state(struct switch_dev *sdev, uint32_t state)
{
	char *envp[3];
	char name_buffer[MAX_UEVENT_BUFFER_LENGTH] = {0};
	char state_buffer[MAX_UEVENT_BUFFER_LENGTH] = {0};
	int length;

	if (sdev == NULL) {
		dpu_pr_err("[DP] sdev is NULL!\n");
		return;
	}

	length = snprintf_s(name_buffer, sizeof(name_buffer), sizeof(name_buffer) - 1, "SWITCH_NAME=%s", sdev->name);
	if (length < 0 || length >= MAX_UEVENT_BUFFER_LENGTH) {
		dpu_pr_err("[DP] fatal: snprintf_s name_buffer fail, length=%d\n", length);
		return;
	}

	length = snprintf_s(state_buffer, sizeof(state_buffer), sizeof(state_buffer) - 1, "SWITCH_STATE=%u", state);
	if (length < 0 || length >= MAX_UEVENT_BUFFER_LENGTH) {
		dpu_pr_err("[DP] fatal: snprintf_s state_buffer fail, length=%d\n", length);
		return;
	}

	envp[0] = name_buffer;
	envp[1] = state_buffer;
	envp[2] = NULL;
	kobject_uevent_env(&sdev->dev->kobj, KOBJ_CHANGE, envp);

	dpu_pr_info("[DP] Notification: %s, %s.\n", name_buffer, state_buffer);
}

bool dptx_is_usb_dp_panel(int port_id)
{
	return !(port_id == DPTX_PORT_ID_4 || port_id == DPTX_PORT_ID_5);
}

void dptx_wait_for_last_frame_finished(struct dkmd_connector_info *pinfo, enum dptx_wait_handle_type handle_type)
{
	uint32_t wait_time = DEFAULT_WAIT_TIME;

	/*
	* Wait for two frames time to make sure that the last frame is sent out before the new timing parameters
	* are applied when setting the physical display timing parameters. This is to avoid the underflow after
	* the new timing parameters are applied. The maximum waiting time is 100ms.
	*/
	if (pinfo->base.fps != 0) {
		wait_time = 2 * (1000 / pinfo->base.fps) + DELAY_MARGIN;
		wait_time = (wait_time > MAX_WAIT_TIME ? MAX_WAIT_TIME : wait_time);
	}

	if (handle_type == DELAY_HANDLE) {
		dpu_pr_info("[DP] Delay %u ms for last frame finish, fps:%u Hz", wait_time, pinfo->base.fps);
		mdelay(wait_time);
	}

	if (handle_type == SLEEP_HANDLE) {
		dpu_pr_info("[DP] Sleep %u ms for last frame finish, fps:%u Hz", wait_time, pinfo->base.fps);
		msleep(wait_time);
	}
}