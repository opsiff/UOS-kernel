
obj-$(CONFIG_DRMDRIVER)                   += drmdriver/
obj-$(CONFIG_JPU_SUPPORT) += jpu/

obj-$(CONFIG_VFB_BE) += cdc_rda/vfb/
obj-$(CONFIG_DPU_INDEP_RST_VM_CTRL) += cdc_rda/indep_rst/

ifeq ($(CONFIG_RDA_DISPLAY_DRIVERS),y)
obj-y += cdc_rda/drm/ # rda drm driver
else
# dpu_drm/ 仅提供 Kconfig 符号；实现位于 drivers/video/fbdev/{dkmd,ukmd}
endif