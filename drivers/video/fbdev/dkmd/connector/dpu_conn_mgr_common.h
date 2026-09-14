/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.
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

 #ifndef DPU_CONN_MGR_COMMON_H
#define DPU_CONN_MGR_COMMON_H

#include "dpu_connector.h"
#include "dpu_conn_mgr.h"
#include <linux/mutex.h>

int32_t connector_manager_disconnect(struct dkmd_connector_info *pinfo);
int32_t disconnect_post_handle(struct dkmd_connector_info *pinfo, char __iomem *dpu_base);
int32_t connector_manager_connect(struct dkmd_connector_info *pinfo);

static inline int count_connector_num(void)
{
	int ret = 0;
	int connector_idx = 0;
	struct dpu_connector *connector = NULL;
	for (connector_idx = 0; connector_idx < CONNECTOR_ID_MAX; connector_idx++) {
		connector = g_conn_manager->connector[connector_idx % CONNECTOR_ID_MAX];
		if (connector != NULL && connector->is_connected)
			ret++;
	}
	return ret;
}

static inline int get_connector_num(void)
{
	int ret = 0;
	mutex_lock(&g_conn_manager->connect_status_mutex);
	ret = count_connector_num();
	mutex_unlock(&g_conn_manager->connect_status_mutex);
	return ret;
}

int clear_pip_sw_config(char __iomem *dpu_base, struct dkmd_connector_info *pinfo, int post_chn);

#endif