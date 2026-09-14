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

#include <linux/delay.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/hwspinlock.h>
#include <dpu/soc_dpu_define.h>
#include "dpu/dpu_base_addr.h"
#include "dpu_comp_tunnel_power_ctl.h"
#include "dpu_config_utils.h"
#include "dkmd_log.h"
#include "dpu_conn_mgr.h"
#include "dpu_comp_mgr.h"
#include "dpu_connector.h"

#define DPU_AO_LOCK_ID		 79
#define DPU_AO_LOCK_TIMEOUT  50
#define VOTE_WAIT_MAX_CNT    50
enum dpu_tunnel_vote_reg_bit {
	SCBAKDATA11_SENSORHUB_VOTE_BIT = BIT(26),
	SCBAKDATA11_DPU_AP_ONLINE_BIT = BIT(27),
};
static struct hwspinlock *g_dpu_tunnel_hwlock = NULL;

void dpu_tunnel_hwspinlock_init(void)
{
	g_dpu_tunnel_hwlock = hwspin_lock_request_specific(DPU_AO_LOCK_ID);
	if (g_dpu_tunnel_hwlock == NULL) {
		dpu_pr_err("dss get hwspinlock fail\n");
		return;
	}
}

static int32_t dpu_tunnel_hardlock_get(void)
{
	dpu_check_and_return(!g_dpu_tunnel_hwlock, -1, err, "get tunnel hwlock is NULL");
	return hwspin_lock_timeout(g_dpu_tunnel_hwlock, DPU_AO_LOCK_TIMEOUT);
}

static void dpu_tunnel_hardlock_put(void)
{
	dpu_check_and_no_retval(!g_dpu_tunnel_hwlock, err, "put tunnel hwlock is NULL");
	hwspin_unlock(g_dpu_tunnel_hwlock);
}

static int32_t dpu_tunnel_set_dpu_online_reg(bool online)
{
	uint32_t value = 0;
	char __iomem *sctrl_base = dpu_config_get_ip_base(DISP_IP_BASE_SCTRL);
	dpu_check_and_return(!sctrl_base, -1, err, "sctrl_base is NULL");

	if (dpu_tunnel_hardlock_get()) {
		dpu_pr_err("get hwspinlock fail\n");
		return -1;
	}
	value = inp32(SOC_SCTRL_SCBAKDATA11_ADDR(sctrl_base));
	if (online)
		value |= SCBAKDATA11_DPU_AP_ONLINE_BIT;
	else
		value &= (~SCBAKDATA11_DPU_AP_ONLINE_BIT);
	outp32(SOC_SCTRL_SCBAKDATA11_ADDR(sctrl_base), value);
	dpu_tunnel_hardlock_put();
	dpu_pr_debug("online %d, SOC_SCTRL_SCBAKDATA3_MSK_ADDR value = 0x%x", online, value);
	return 0;
}

static int32_t dpu_tunnel_enter_idle_proc(void)
{
	uint32_t value = 0;
	char __iomem *sctrl_base = dpu_config_get_ip_base(DISP_IP_BASE_SCTRL);
	dpu_check_and_return(!sctrl_base, -1, err, "sctrl_base is NULL");

	if (dpu_tunnel_hardlock_get()) {
		dpu_pr_err("get hwspinlock fail\n");
		return -1;
	}
	/* check sensorhub present vote bit */
	value = inp32(SOC_SCTRL_SCBAKDATA11_ADDR(sctrl_base));
	dpu_pr_debug("SOC_SCTRL_SCBAKDATA3_MSK_ADDR value = 0x%x", value);
	if ((value & SCBAKDATA11_SENSORHUB_VOTE_BIT) == SCBAKDATA11_SENSORHUB_VOTE_BIT) {
		dpu_tunnel_hardlock_put();
		dpu_pr_warn("sensorhub has vote, value = 0x%x", value);
		return -1;
	}
	/* clear ap online bit */
	value &= (~SCBAKDATA11_DPU_AP_ONLINE_BIT);
	outp32(SOC_SCTRL_SCBAKDATA11_ADDR(sctrl_base), value);

	dpu_tunnel_hardlock_put();
	dpu_pr_debug("vote result: SOC_SCTRL_SCBAKDATA3_MSK_ADDR value = 0x%x", value);
	return 0;
}

static bool dpu_comp_has_low_power_tunnel_layer(struct dpu_composer *dpu_comp)
{
	struct comp_online_present *present = (struct comp_online_present *)dpu_comp->present_data;
	struct dpu_tunnel_ctrl *tunnel_ctrl = &(present->tunnel_ctrl);
	int32_t layer_cnt = atomic_read(&tunnel_ctrl->dev_commit_layer_cnt);
	dpu_pr_debug("tunnel cnt %d", layer_cnt);
	return (layer_cnt != 0);
}

int32_t dpu_tunnel_proc_enter_idle(struct dpu_composer *dpu_comp)
{
	if (dpu_comp_has_low_power_tunnel_layer(dpu_comp)) {
		dpu_pr_debug("has tunnel layer, do not enter idle");
		return -1;
	}
	if (dpu_tunnel_enter_idle_proc() != 0) {
		dpu_pr_debug("iomcu has vote, do not enter idle");
		return -1;
	}
	return 0;
}

void dpu_tunnel_proc_exit_idle(uint32_t dev_commit_layer_cnt)
{
	if (dev_commit_layer_cnt != 0)
		dpu_tunnel_set_dpu_online_reg(true);
	dpu_pr_debug("-");
}

void dpu_tunnel_proc_power_on(struct dpu_composer *dpu_comp)
{
	if (is_offline_panel(&dpu_comp->comp.base))
		return;
	dpu_tunnel_set_dpu_online_reg(true);
	dpu_pr_debug("-");
}

void dpu_tunnel_proc_power_off(struct dpu_composer *dpu_comp)
{
	uint32_t value = 0;
	uint32_t wait_cnt;
	char __iomem *sctrl_base = dpu_config_get_ip_base(DISP_IP_BASE_SCTRL);
	dpu_check_and_no_retval(!sctrl_base, err, "sctrl_base is NULL");

	if (is_offline_panel(&dpu_comp->comp.base))
		return;
	for (wait_cnt = 0; wait_cnt < VOTE_WAIT_MAX_CNT; wait_cnt++) {
		if (dpu_tunnel_hardlock_get()) {
			dpu_pr_err("get hwspinlock fail\n");
			return;
		}
		value = inp32(SOC_SCTRL_SCBAKDATA11_ADDR(sctrl_base));
		dpu_pr_debug("SOC_SCTRL_SCBAKDATA3_MSK_ADDR value = 0x%x", value);
		/* wait sensorhub present release vote */
		if ((value & SCBAKDATA11_SENSORHUB_VOTE_BIT) != SCBAKDATA11_SENSORHUB_VOTE_BIT) {
			/* clear ap online bit */
			value &= (~SCBAKDATA11_DPU_AP_ONLINE_BIT);
			outp32(SOC_SCTRL_SCBAKDATA11_ADDR(sctrl_base), value);
			dpu_tunnel_hardlock_put();
			return;
		}
		dpu_tunnel_hardlock_put();
		usleep_range(50, 100); /* 50 ~ 100 us */
	}
	dpu_pr_warn("dpu tunnel wait power off vote timeout!");
	/* clear ap online bit */
	dpu_tunnel_set_dpu_online_reg(false);
}