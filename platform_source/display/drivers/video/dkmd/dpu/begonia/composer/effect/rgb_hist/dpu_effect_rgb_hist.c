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

#include <securec.h>
#include <linux/types.h>
#include <linux/fb.h>

#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>

#include "dpu_comp_mgr.h"
#include "effect/dpu_effect_init.h"
#include "dpu_effect_rgb_hist.h"
#include "dkmd_mipi_panel_info.h"

enum hist_op {
    HIST_OP_INIT,
    HIST_OP_DEINIT,
    HIST_OP_UPDATE,
};

static void dpu_read_rgb_hist(struct dkmd_rgb_hist_ctrl *rgb_hist_ctrl)
{
	int i;
	uint32_t *hist_data_ptr = rgb_hist_ctrl->hist_data.rgb_hist;

	for (i = 0; i < RGB_HIST_SIZE; i++) {
		hist_data_ptr[i] = inp32(DPU_DPP_R0_HIST_ADDR(rgb_hist_ctrl->dpp_base + i * 4));
	}
}

static void dpu_hist_status_operation(struct dkmd_rgb_hist_ctrl *rgb_hist_ctrl, enum hist_op op)
{
	dpu_pr_debug("[rgb_hist] op=%d", op);
	switch (op) {
	case HIST_OP_INIT:
        if (!rgb_hist_ctrl->hist_status_inited) {
            rgb_hist_ctrl->hist_status.hist_stop = false;
            rgb_hist_ctrl->hist_status_inited = true;
        }
        break;
    case HIST_OP_DEINIT:
        if (rgb_hist_ctrl->hist_status_inited) {
            rgb_hist_ctrl->hist_status_inited = false;
            rgb_hist_ctrl->hist_status.hist_stop = true;
            wake_up_interruptible(&rgb_hist_ctrl->hist_status.wq_hist);
        }
        break;
    case HIST_OP_UPDATE:
        if (rgb_hist_ctrl->hist_status_inited) {
            rgb_hist_ctrl->hist_status.new_hist = true;
            wake_up_interruptible(&rgb_hist_ctrl->hist_status.wq_hist);
        }
        break;
    }
}

static void dpu_hist_status_init(struct dkmd_rgb_hist_ctrl *rgb_hist_ctrl)
{
    dpu_hist_status_operation(rgb_hist_ctrl, HIST_OP_INIT);
}

static void dpu_hist_status_deinit(struct dkmd_rgb_hist_ctrl *rgb_hist_ctrl)
{
	mutex_lock(&rgb_hist_ctrl->hist_data_lock);
    dpu_hist_status_operation(rgb_hist_ctrl, HIST_OP_DEINIT);
	mutex_unlock(&rgb_hist_ctrl->hist_data_lock);
}

static void dpu_hist_status_update(struct dkmd_rgb_hist_ctrl *rgb_hist_ctrl)
{
    dpu_hist_status_operation(rgb_hist_ctrl, HIST_OP_UPDATE);
}

static void dpu_effect_rgb_hist_handle_work(struct kthread_work *work)
{
	uint64_t tv0;
	struct dpu_composer *dpu_comp = NULL;
	struct dkmd_rgb_hist_ctrl *rgb_hist_ctrl = NULL;

	dpu_comp = container_of(work, struct dpu_composer, rgb_hist_work);
	dpu_check_and_no_retval(!dpu_comp, err, "dpu_comp is NULL\n");

	rgb_hist_ctrl = dpu_comp->rgb_hist_ctrl;
	dpu_check_and_no_retval(!rgb_hist_ctrl, err, "rgb_hist_ctrl is NULL\n");

	dpu_trace_ts_begin(&tv0);

	down(&dpu_comp->comp.blank_sem);
	dpu_print_sem_count(&dpu_comp->comp.blank_sem, true);
	if (!dpu_comp->comp.power_on) {
		dpu_pr_debug("panel power off");
		dpu_print_sem_count(&dpu_comp->comp.blank_sem, false);
		up(&dpu_comp->comp.blank_sem);
		return;
	}

	mutex_lock(&rgb_hist_ctrl->hist_data_lock);
	dpu_comp_active_vsync(dpu_comp);
	dpu_hist_status_init(rgb_hist_ctrl);
	dpu_read_rgb_hist(rgb_hist_ctrl);
	dpu_comp_deactive_vsync(dpu_comp);
	dpu_hist_status_update(rgb_hist_ctrl);
	mutex_unlock(&rgb_hist_ctrl->hist_data_lock);
	dpu_print_sem_count(&dpu_comp->comp.blank_sem, false);
	up(&dpu_comp->comp.blank_sem);

	dpu_trace_ts_end(&tv0, "rgb_hist handle finished!");
}

static int32_t dpu_effect_rgb_hist_get_hist(struct dkmd_rgb_hist_ctrl *rgb_hist_ctrl, void __user* argp)
{
    int ret = 0;
    long wait_ret = 0;
    unsigned long timeout = msecs_to_jiffies(100000);
    struct rgb_hist_status *status = NULL;
    bool has_new_hist = false;
	int times = 0;

	dpu_check_and_return(unlikely(!argp), -EINVAL, err, "[rgb_hist] argp is NULL");
	dpu_check_and_return(unlikely(!rgb_hist_ctrl), -EINVAL, err, "[rgb_hist] rgb_hist_ctrl is NULL");

    status = &rgb_hist_ctrl->hist_status;
	while (1) {
		wait_ret = wait_event_interruptible_timeout(
			status->wq_hist,
			status->new_hist || status->hist_stop,
			(long)timeout);
		if ((wait_ret == -ERESTARTSYS) && (times++ < 100)) {
				dpu_pr_debug("[rgb_hist] erestartsys error");
				mdelay(5);
		} else {
			if (times != 0)
				dpu_pr_warn("[rgb_hist] wait_ret is -ERESTARTSYS, max times=%d", times);
			break;
		}
	}

	// avoid using mutex_lock() but hist_lock was destoried by mutex_destory in deinit
    if (down_interruptible(&rgb_hist_ctrl->hist_lock_sem)) {
        dpu_pr_warn("[rgb_hist] semaphore interrupted");
        return -ERESTARTSYS;
    }

	mutex_lock(&rgb_hist_ctrl->hist_data_lock);
	has_new_hist = status->new_hist;
	status->new_hist = false;
	status->hist_stop = false;
    if (has_new_hist) {
        ret = (int)copy_to_user(argp, &rgb_hist_ctrl->hist_data, sizeof(rgb_hist_ctrl->hist_data));
        if (ret) {
            dpu_pr_warn("[rgb_hist] copy_to_user failed! ret=%d", ret);
            ret = -EFAULT;
        }
        
    } else {
        ret = handle_err_hist((int32_t)wait_ret);
    }
	mutex_unlock(&rgb_hist_ctrl->hist_data_lock);
    up(&rgb_hist_ctrl->hist_lock_sem);

    return ret;
}

static void dpu_effect_rgb_hist_queue_work(struct dpu_composer *dpu_comp)
{
	kthread_queue_work(&dpu_comp->effect_worker, &dpu_comp->rgb_hist_work);
}

void dpu_effect_rgb_hist_init(struct dpu_composer *dpu_comp, uint32_t dpp_offset)
{
	struct dkmd_rgb_hist_ctrl *rgb_hist_ctrl = NULL;
	struct rgb_hist_status *status = NULL;

	dpu_check_and_no_retval(!dpu_comp, err, "dpu_comp is NULL\n");

	if (IS_ERR_OR_NULL(dpu_comp->effect_thread)) {
		dpu_pr_err("effect_thread is not valid!");
		return;
	}

	if (dpu_comp->rgb_hist_ctrl) {
		dpu_pr_warn("rgb_hist already inited");
		return;
	}

	rgb_hist_ctrl = kzalloc(sizeof(struct dkmd_rgb_hist_ctrl), GFP_KERNEL);
	if (!rgb_hist_ctrl) {
		dpu_pr_err("alloc rgb_hist ctrl failed");
		return;
	}

	status = &rgb_hist_ctrl->hist_status;
	if (memset_s(status, sizeof(*status), 0, sizeof(*status)) != 0) {
		dpu_pr_err("alloc rgb_hist status memset failed");
		return;
	}

	rgb_hist_ctrl->dpp_base = dpu_comp->comp_mgr->dpu_base + dpp_offset;
	rgb_hist_ctrl->queue_work = dpu_effect_rgb_hist_queue_work;
	rgb_hist_ctrl->get_hist = dpu_effect_rgb_hist_get_hist;
	rgb_hist_ctrl->wake_up_hist = dpu_hist_status_deinit;

	sema_init(&rgb_hist_ctrl->hist_lock_sem, 1);
	mutex_init(&rgb_hist_ctrl->hist_data_lock);
	init_waitqueue_head(&status->wq_hist);
	kthread_init_work(&dpu_comp->rgb_hist_work, dpu_effect_rgb_hist_handle_work);
	dpu_comp->rgb_hist_ctrl = rgb_hist_ctrl;
}

void dpu_effect_rgb_hist_deinit(struct dpu_composer *dpu_comp)
{
	struct dkmd_rgb_hist_ctrl *rgb_hist_ctrl = NULL;

	dpu_check_and_no_retval(!dpu_comp, err, "dpu_comp is NULL\n");

	if (!dpu_comp->rgb_hist_ctrl) {
		dpu_pr_warn("rgb_hist not inited");
		return;
	}

	rgb_hist_ctrl = dpu_comp->rgb_hist_ctrl;

	down(&rgb_hist_ctrl->hist_lock_sem);
	mutex_destroy(&rgb_hist_ctrl->hist_data_lock);
	up(&rgb_hist_ctrl->hist_lock_sem);

	kfree(dpu_comp->rgb_hist_ctrl);
	dpu_comp->rgb_hist_ctrl = NULL;
}