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

#include <dpu/soc_dpu_define.h>
#include "dkmd_object.h"
#include "dkmd_log.h"
#include "dpu_comp_dfr_config_utils.h"
#include "cmdlist_interface.h"
#include "ukmd_cmdlist.h"
#include "dpu_comp_dfr_te_skip.h"
#include "dpu_conn_mgr.h"
#include "dpu_comp_mgr.h"

void dpu_dacc_update_frame_rate_info(char __iomem *dpu_base, uint32_t frm_rate, uint32_t te_rate, uint32_t te_mask_num)
{
	/* update frame rate to dacc */
	outp32(DPU_DFR_FRM_RATE_ADDR(dpu_base), frm_rate);
	/* update te rate to dacc */
	outp32(DPU_DFR_TE_RATE_ADDR(dpu_base), te_rate);
	/* update te mask to dacc */
	outp32(DPU_DFR_TE_MASK_NUM_ADDR(dpu_base), te_mask_num);
}

#define DACC_DFR_RAM_SIZE 0x1FF
void dpu_dacc_dfr_setup_data(char __iomem *dpu_base, uint32_t lcd_te_idx, uint32_t init_config)
{
	uint32_t i = 0;
	/* clear ltpo ram, DPU_DFR_ENABLE_ADDR is start addr */
	for (i = 0; i < DACC_DFR_RAM_SIZE; i += 4)
		outp32(DPU_DFR_ENABLE_ADDR(dpu_base) + i, 0);
	dpu_dacc_clear_multi_config(dpu_base);
	outp32(DPU_DFR_TE_IDX_ADDR(dpu_base), lcd_te_idx);
	/* unmask acpu notify dacc intr */
	outp32(DPU_RISCV_INTR_MASK_ADDR(dpu_base), 0xFF00);
	/* notifiy dacc resume to dacc 0x80： bit7 is acpu interrupt */
	outp32(DPU_RISCV_INTR_TRIG_ADDR(dpu_base), 0x80);

	outp32(DPU_DFR_INIT_CONFIG_ADDR(dpu_base), init_config);
}


void dpu_dacc_set_dfr_enable(char __iomem *dpu_base, uint32_t flag)
{
	outp32(DPU_DFR_ENABLE_ADDR(dpu_base), flag);
}

void dpu_dacc_set_need_wait_te_num(char __iomem *dpu_base, uint32_t skip_num)
{
	/* update skip te num to dacc */
	outp32(MULTI_NEED_WAIT_TE_NUM_ADDR(dpu_base), skip_num);
}

#define DACC_MULTI_PRESENT_RAM_SIZE 0x20
void dpu_dacc_clear_multi_config(char __iomem *dpu_base)
{
	uint32_t i = 0;
	for (i = 0; i < DACC_MULTI_PRESENT_RAM_SIZE; i += 4)
		outp32(MULTI_CAPTURE_TIME_ADDR(dpu_base) + i, 0);
}

int32_t dpu_dacc_create_cmdlist(struct cmdlist_config *cmdlist, int32_t cmdlist_type)
{
	int32_t ret;
	uint32_t cmdlist_dev_id = CMDLIST_DEV_ID_DPU;
	uint32_t header_cmdlist_id = 0;
	uint32_t reg_cmdlist_id = 0;

	header_cmdlist_id =
		cmdlist_create_user_client(cmdlist_dev_id, cmdlist->cmdlist_scene_id, SCENE_NOP_TYPE, 0, 0);
	if (unlikely(header_cmdlist_id == 0)) {
		dpu_pr_err("scene_id=%u, create header cmdlist fail", cmdlist->cmdlist_scene_id);
		return -1;
	}

	reg_cmdlist_id =
		cmdlist_create_user_client(cmdlist_dev_id, cmdlist->cmdlist_scene_id, REGISTER_CONFIG_TYPE, 0, PAGE_SIZE);
	if (unlikely(reg_cmdlist_id == 0)) {
		ukmd_cmdlist_release_locked(cmdlist_dev_id, cmdlist->cmdlist_scene_id, header_cmdlist_id);
		dpu_pr_err("scene_id=%u create reg cmdlist fail", cmdlist->cmdlist_scene_id);
		return -1;
	}

	ret = cmdlist_append_client(cmdlist_dev_id, cmdlist->cmdlist_scene_id,
		header_cmdlist_id, reg_cmdlist_id);
	if (unlikely(ret != 0)) {
		ukmd_cmdlist_release_locked(cmdlist_dev_id, cmdlist->cmdlist_scene_id,reg_cmdlist_id);
		ukmd_cmdlist_release_locked(cmdlist_dev_id, cmdlist->cmdlist_scene_id, header_cmdlist_id);
		dpu_pr_err("append reg(%u) cmdlist fail", reg_cmdlist_id);
		return -1;
	}

	cmdlist->header_cmdlist_ids[cmdlist_type] = header_cmdlist_id;
	cmdlist->reg_cmdlist_ids[cmdlist_type] = reg_cmdlist_id;
	return 0;
}

#define BIT_LDI_EN 0
#define BIT_DUAL_LDI_EN 5
int32_t dpu_dacc_set_reg_dsi_ldi(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct cmdlist_config *cmdlist, int32_t cmdlist_type)
{
	uint32_t val = 0;
	uint32_t connector_offset = 0;
	struct dpu_connector *connector = NULL;
	uint32_t cmdlist_dev_id;
	struct dkmd_connector_info *pinfo = dfr_ctrl->dpu_comp->conn_info;

	dpu_check_and_return(!pinfo, -1, err, "pinfo is null");

	connector = get_primary_connector(pinfo);
	dpu_check_and_return(!connector, -1, err, "connector is null");

	connector_offset = g_connector_offset[connector->connector_id];
	cmdlist_dev_id = CMDLIST_DEV_ID_DPU;
	ukmd_set_reg(cmdlist_dev_id, cmdlist->cmdlist_scene_id, cmdlist->reg_cmdlist_ids[cmdlist_type],
		DPU_DSI_LDI_FRM_MSK_UP_ADDR(connector_offset), 0x1);
	if (connector->bind_connector)
		ukmd_set_reg(cmdlist_dev_id, cmdlist->cmdlist_scene_id, cmdlist->reg_cmdlist_ids[cmdlist_type],
			DPU_DSI_LDI_FRM_MSK_UP_ADDR(g_connector_offset[connector->bind_connector->connector_id]),
			0x1);

	val = inp32(DPU_DSI_LDI_CTRL_ADDR(connector->connector_base));
	if (connector->bind_connector) {
		val |= BIT(BIT_DUAL_LDI_EN);
		ukmd_set_reg(cmdlist_dev_id, cmdlist->cmdlist_scene_id, cmdlist->reg_cmdlist_ids[cmdlist_type],
			DPU_DSI_LDI_CTRL_ADDR(connector_offset), val);
	} else {
		val |= BIT(BIT_LDI_EN);
		ukmd_set_reg(cmdlist_dev_id, cmdlist->cmdlist_scene_id, cmdlist->reg_cmdlist_ids[cmdlist_type],
			DPU_DSI_LDI_CTRL_ADDR(connector_offset), val);
	}

	return 0;
}

int32_t dpu_dacc_commit_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct cmdlist_config *cmdlist)
{
	char __iomem *dpu_base = NULL;
	dma_addr_t phy_addr = 0;
	dpu_check_and_return(!dfr_ctrl->dpu_comp->comp_mgr, -1, err, "comp_mgr is null\n");
	dpu_base = dfr_ctrl->dpu_comp->comp_mgr->dpu_base;
	dpu_pr_debug("active id: %u", cmdlist->active_id);

	phy_addr = cmdlist_get_phy_addr(CMDLIST_DEV_ID_DPU, cmdlist->cmdlist_scene_id, cmdlist->active_id);
	if (phy_addr == 0) {
		dpu_pr_err("invalid active cmdlist %u, commit failed", cmdlist->active_id);
		return -1;
	}

	outp32(DPU_DFR_PRESENT_ADDR(dpu_base), phy_addr);
	/* notifiy frame update to dacc 0x10: bit4 is acpu interrupt */
	outp32(DPU_RISCV_INTR_TRIG_ADDR(dpu_base), 0x10);

	return 0;
}

void dpu_dacc_setup_priv_data_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct cmdlist_config *cmdlist)
{
	char __iomem *dpu_base = NULL;
	dfr_init_config_union init_config;
	uint32_t lcd_te_idx = 0;
	uint32_t te_rate = 0;
	struct dfr_info* dinfo = dkmd_get_dfr_info(dfr_ctrl->dpu_comp->conn_info);
	dpu_check_and_no_retval(!dinfo, err, "dinfo is null");

	dfr_ctrl->unsafe_period = 300; /* us */
	cmdlist->cmdlist_scene_id = DPU_SCENE_ONLINE_3;

	if (dpu_dacc_create_cmdlist(cmdlist, CMDLIST_LDI_BY_MCU_SEND_FRM) != 0 ||
		dpu_dacc_set_reg_dsi_ldi(dfr_ctrl, cmdlist, CMDLIST_LDI_BY_MCU_SEND_FRM))
		return;
	cmdlist->active_id = cmdlist->reg_cmdlist_ids[CMDLIST_LDI_BY_MCU_SEND_FRM];
	dpu_base = dfr_ctrl->dpu_comp->comp_mgr->dpu_base;
	lcd_te_idx = dfr_ctrl->dpu_comp->conn_info->base.lcd_te_idx;

	init_config.value = 0;
	init_config.param.self_refresh_rate = dfr_ctrl->dpu_comp->conn_info->base.fps;
	init_config.param.enable_multi_present = 1;
	init_config.param.oled_type = dinfo->oled_info.oled_type;

	te_rate = dfr_ctrl->cur_frm_rate;
	dpu_pr_info("ltps setup data init_config = %#x", init_config.value);
	dpu_dacc_dfr_setup_data(dpu_base, lcd_te_idx, init_config.value);
	dpu_dacc_update_frame_rate_info(dpu_base, dfr_ctrl->cur_frm_rate, te_rate, 0);
	/* enable dacc dfr */
	dpu_dacc_set_dfr_enable(dpu_base, 1);
}

void dpu_dacc_release_data_by_mcu(struct dpu_comp_dfr_ctrl *dfr_ctrl, struct cmdlist_config *cmdlist)
{
	struct dpu_composer *dpu_comp = dfr_ctrl->dpu_comp;
	char __iomem *dpu_base = dpu_comp->comp_mgr->dpu_base;
	int32_t index;

	dpu_pr_info("+");

	for (index = 0; index < CMDLIST_LDI_BY_MCU_TYPE_MAX; index++) {
		if (cmdlist->header_cmdlist_ids[index] != 0) {
			ukmd_cmdlist_release_locked(CMDLIST_DEV_ID_DPU,
				cmdlist->cmdlist_scene_id, cmdlist->header_cmdlist_ids[index]);
			cmdlist->header_cmdlist_ids[index] = 0;
		}
	}
	/* disable dacc dfr */
	dpu_dacc_set_dfr_enable(dpu_base, 0);
	dpu_pr_info("-");
}