/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2012-2021. All rights reserved.
 *
 * jpgdec irq
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

#include "jpu.h"
#include "jpu_def.h"
#include "jpu_utils.h"
#include "jpu_osal.h"
#include "jpu_irq.h"

#define JPEGDEC_FINSH_BIT 0
#define JPEGDEC_ERROR_BIT 1
#define JPEGDEC_OVERTIME_BIT 2
#define JPEGDEC_BS_RESUME_BIT 3

static void jpgdec_abnormal_irq(struct jpu_data_type *jpu_device, uint32_t bit)
{
	// dump register before soft reset
	jpu_dump_reg(jpu_device);
	uint32_t reg_val = inp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG0);
	reg_val |= BIT(bit);
	outp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG0, reg_val);
}

static irqreturn_t jpgdec_irq_isr(int32_t irq, void *ptr)
{
	struct jpu_data_type *jpu_device = NULL;
	uint32_t reg_val;
	uint32_t isr_state;

	jpu_check_null_return(ptr, IRQ_HANDLED);
	jpu_device = (struct jpu_data_type *)ptr;
	jpu_check_null_return(jpu_device->jpu_top_base, IRQ_HANDLED);

	isr_state = inp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG2);
	if (isr_state & BIT(DEC_DONE_ISR_BIT)) { /* use 16bit to decide */
		jpu_info("done isr occured\n");
		reg_val = inp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG0);
		reg_val |= BIT(JPEGDEC_FINSH_BIT);
		outp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG0, reg_val);
		jpu_device->jpu_dec_done_flag = DEC_FINISH_FLAG;
		jpu_wake_up_interrupt((struct jpu_interrupt_t *)(jpu_device->jpu_irq_num));
	}

	if (isr_state & BIT(DEC_ERR_ISR_BIT)) { /* use 17bit to decide */
		jpu_info("err isr occured\n");
		jpgdec_abnormal_irq(jpu_device, JPEGDEC_ERROR_BIT);
	}

	if (isr_state & BIT(DEC_OVERTIME_ISR_BIT)) { /* use 18bit to decide */
		jpu_info("overtime isr occured\n");
		jpgdec_abnormal_irq(jpu_device, JPEGDEC_OVERTIME_BIT);
	}

	if (isr_state & BIT(DEC_BS_RESUME_BIT)) { /* use 19bit to decide */
		jpu_info("bitstream resume isr occured\n");
		jpgdec_abnormal_irq(jpu_device, JPEGDEC_BS_RESUME_BIT);
	}

	return IRQ_HANDLED;
}

int32_t jpgdec_request_irq(struct jpu_data_type *jpu_device)
{
	int32_t index;
	int32_t ret = 0;

	jpu_check_null_return(jpu_device, -EINVAL);

	for (index = 0; index < JPGDEC_IRQ_NUM; index++) {
		ret = os_request_irq(jpu_device->jpu_irq_num[index], jpgdec_irq_isr, (void *)jpu_device);
		if (ret != 0) {
			jpu_err("request irq failed, irq_num = %d error = %d\n", jpu_device->jpu_irq_num[index], ret);
			return ret;
		}
		os_disable_irq(jpu_device->jpu_irq_num[index]);
	}
	return ret;
}

int32_t jpgdec_enable_irq(struct jpu_data_type *jpu_device)
{
	int32_t index;
	jpu_check_null_return(jpu_device, -EINVAL);

	for (index = 0; index < JPGDEC_IRQ_NUM; index++) {
		if (jpu_device->jpu_irq_num[index] == 0)
			return -EINVAL;
		os_enable_irq(jpu_device->jpu_irq_num[index]);
	}

	return 0;
}

void jpgdec_disable_irq(struct jpu_data_type *jpu_device)
{
	int32_t index;
	if (jpu_device == NULL) {
		jpu_err("jpu_device is nullptr\n");
		return;
	}

	for (index = 0; index < JPGDEC_IRQ_NUM; index++) {
		if (jpu_device->jpu_irq_num[index] != 0)
			os_disable_irq(jpu_device->jpu_irq_num[index]);
	}
}

void jpgdec_free_irq(struct jpu_data_type *jpu_device)
{
	int32_t index;
	if (jpu_device == NULL) {
		jpu_err("jpu_device is nullptr\n");
		return;
	}

	for (index = 0; index < JPGDEC_IRQ_NUM; index++) {
		if (jpu_device->jpu_irq_num[index] != 0) {
			os_free_irq(jpu_device->jpu_irq_num[index], jpu_device);
			jpu_device->jpu_irq_num[index] = 0;
		}
	}
}

void jpu_dec_interrupt_unmask(const struct jpu_data_type *jpu_device)
{
	uint32_t unmask;

	unmask = ~0;
	unmask &= ~(BIT_JPGDEC_INT_DEC_ERR | BIT_JPGDEC_INT_DEC_FINISH);

	outp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG1, unmask);
}

void jpu_dec_interrupt_mask(const struct jpu_data_type *jpu_device)
{
	uint32_t mask;

	mask = ~0;
	outp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG1, mask);
}

void jpu_dec_interrupt_clear(const struct jpu_data_type *jpu_device)
{
	/* clear jpg decoder IRQ state
	 * [3]: jpgdec_int_over_time;
	 * [2]: jpgdec_int_dec_err;
	 * [1]: jpgdec_int_bs_res;
	 * [0]: jpgdec_int_dec_finish;
	 */
	outp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG0, 0xF); /* 0xF clera irq */
}

static void dump_top_reg(struct jpu_data_type *jpu_device)
{
	jpu_info("dump top_reg: \n");
	jpu_info("JPGDEC_RO_STATE: 0x%x\n",
		inp32(jpu_device->jpu_top_base + JPGDEC_RO_STATE));
	jpu_info("JPGDEC_CRG_CFG0: 0x%x\n",
		inp32(jpu_device->jpu_top_base + JPGDEC_CRG_CFG0));
	jpu_info("JPGDEC_CRG_CFG1: 0x%x\n",
		inp32(jpu_device->jpu_top_base + JPGDEC_CRG_CFG1));
	jpu_info("JPGDEC_MEM_CFG: 0x%x\n",
		inp32(jpu_device->jpu_top_base + JPGDEC_MEM_CFG));
	jpu_info("JPGDEC_IRQ_REG0: 0x%x\n",
		inp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG0));
	jpu_info("JPGDEC_IRQ_REG1: 0x%x\n",
		inp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG1));
	jpu_info("JPGDEC_IRQ_REG2: 0x%x\n",
		inp32(jpu_device->jpu_top_base + JPGDEC_IRQ_REG2));
}

void jpu_dump_reg(struct jpu_data_type *jpu_device)
{
	int32_t i;

	if (jpu_device == NULL) {
		jpu_err("jpu_device is NULL\n");
		return;
	}

	jpu_info("dump debug_reg:\n");
	for (i = 0; i < JPGD_REG_DEBUG_RANGE; i++)
		jpu_info("JPEG_DEC_DEBUG_INFO offset @ %d, val:0x%x\n",
			NUM_COMPS_IN_SCAN * i, inp32(jpu_device->jpu_dec_base +
			JPGD_REG_DEBUG_BASE + NUM_COMPS_IN_SCAN * i));

	jpu_info("dump cvdr_reg: \n");
	if ((jpu_device->jpu_support_platform == DSS_V400) ||
		(jpu_device->jpu_support_platform == DSS_V501))
		jpu_info("JPGDEC_CVDR_AXI_JPEG_CVDR_CFG: 0x%x\n",
			inp32(jpu_device->jpu_cvdr_base + JPGDEC_CVDR_AXI_JPEG_CVDR_CFG));

	jpu_info("JPGDEC_CVDR_AXI_WR_CFG1: 0x%x\n",
		inp32(jpu_device->jpu_cvdr_base + JPGDEC_CVDR_AXI_WR_CFG1));
	jpu_info("JPGDEC_CVDR_AXI_WR_CFG2: 0x%x\n",
		inp32(jpu_device->jpu_cvdr_base + JPGDEC_CVDR_AXI_WR_CFG2));
	jpu_info("JPGDEC_CVDR_AXI_LIMITER_RD_CFG1: 0x%x\n",
		inp32(jpu_device->jpu_cvdr_base + JPGDEC_CVDR_AXI_LIMITER_RD_CFG1));
	jpu_info("JPGDEC_CVDR_AXI_RD_CFG1: 0x%x\n",
		inp32(jpu_device->jpu_cvdr_base + JPGDEC_CVDR_AXI_RD_CFG1));
	jpu_info("JPGDEC_CVDR_AXI_LIMITER_RD_CFG2: 0x%x\n",
		inp32(jpu_device->jpu_cvdr_base + JPGDEC_CVDR_AXI_LIMITER_RD_CFG2));
	jpu_info("JPGDEC_CVDR_AXI_RD_CFG2: 0x%x\n",
		inp32(jpu_device->jpu_cvdr_base + JPGDEC_CVDR_AXI_RD_CFG2));

	jpu_info("JPGDEC_CVDR_AXI_JPEG_CVDR_WR_QOS_CFG: 0x%x\n",
		inp32(jpu_device->jpu_cvdr_base + JPGDEC_CVDR_AXI_JPEG_CVDR_WR_QOS_CFG));
	jpu_info("JPGDEC_CVDR_AXI_JPEG_CVDR_RD_QOS_CFG: 0x%x\n",
		inp32(jpu_device->jpu_cvdr_base + JPGDEC_CVDR_AXI_JPEG_CVDR_RD_QOS_CFG));

	dump_top_reg(jpu_device);
}
