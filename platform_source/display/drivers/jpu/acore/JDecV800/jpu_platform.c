/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2012-2024. All rights reserved.
 *
 * The differences of V800 jpu.
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
#include <platform_include/basicplatform/linux/soc_spec.h>
#include "soc_spec_common.h"


bool jpu_device_need_bypass(void)
{
#if !defined(JPU_SCOUT) && defined(CONFIG_SOC_PG_INFO)
	struct soc_component_pg_info pg_info;
	get_soc_pg_info_by_index(SOC_COMPONENT_MEDIA2_ISP_INFO, &pg_info);
    switch (pg_info.tailor_strategy) {
		case SOC_COMPONENT_TAILOR_STRATEGY_1:
            jpu_info("efuse_value: 0x%08x, tailor_strategy: %d, bypass jpegdec.",
                pg_info.efuse_value, pg_info.tailor_strategy);
            return true;
		case SOC_COMPONENT_NOT_TAILOR:
			jpu_info("no need to bypass jpegdec\n");
			return false;
		default:
			jpu_err("invalid pg info index: %d", pg_info.tailor_strategy);
			return true;
	}
#endif
	jpu_info("no need to bypass jpu\n");
	return false;
}

int32_t jpu_set_smmu(struct jpu_data_type *jpu_device)
{
	int32_t ret = jpu_device->jpgd_drv->setsid_smmu(jpu_device->jpgd_drv,
		jpu_device->jpgd_secadapt_prop.swid[0], MAX_SECADAPT_SWID_NUM,
		jpu_device->jpgd_secadapt_prop.sid, jpu_device->jpgd_secadapt_prop.ssid_ns);
	if (ret != 0) {
		jpu_err("setsid_smmu failed:%d\n", ret);
		if ((jpu_device->jpgd_drv->disable_smmu(jpu_device->jpgd_drv)) != 0)
			jpu_err("failed to disable smmu\n");
		return ret;
	}

	ret = jpu_device->jpgd_drv->set_pref(jpu_device->jpgd_drv,
		jpu_device->jpgd_secadapt_prop.swid[0], MAX_SECADAPT_SWID_NUM);
	if (ret != 0) {
		jpu_err("set_pref failed:%d\n", ret);
		if ((jpu_device->jpgd_drv->disable_smmu(jpu_device->jpgd_drv)) != 0)
			jpu_err("failed to disable smmu\n");
	}

	return ret;
}
