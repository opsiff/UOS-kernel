ccflags-$(CONFIG_DPU_FB_V410) += -I$(srctree)/platform_source/display/drivers/jpu/include/jpgdecv400
ccflags-$(CONFIG_DPU_FB_V501) += -I$(srctree)/platform_source/display/drivers/jpu/incldue/jpgdecv501
ccflags-$(CONFIG_DPU_FB_V510) += -I$(srctree)/platform_source/display/drivers/jpu/include/jpgdecv511
ccflags-$(CONFIG_DPU_FB_V600) += -I$(srctree)/platform_source/display/drivers/jpu/include/jpgdecv600

ifeq ($(CONFIG_DKMD_DPU_VERSION), 700)
ccflags-y += -I$(srctree)/platform_source/display/drivers/jpu/include/jpgdecv700
PLATFORM := JDecV700
endif

ifeq ($(CONFIG_DKMD_DPU_VERSION), 800)
ccflags-y += -I$(srctree)/platform_source/display/drivers/jpu/include/jpgdecv700
PLATFORM := JDecV800
endif

ifeq ($(CONFIG_DKMD_DPU_VERSION), 820)
ccflags-y += -I$(srctree)/platform_source/display/drivers/jpu/include/jpgdecv700
PLATFORM := JDecV820
endif

ifeq ($(CONFIG_DKMD_DPU_VERSION), 840)
ccflags-y += -I$(srctree)/platform_source/display/drivers/jpu/include/jpgdecv700
PLATFORM := JDecV840
endif

ifeq ($(CONFIG_DKMD_DPU_VERSION), 900)
ccflags-y += -I$(srctree)/platform_source/display/drivers/jpu/include/jpgdecv700
PLATFORM := JDecV900
endif

ifeq ($(CONFIG_DKMD_DPU_VERSION), 9200)
ccflags-y += -I$(srctree)/platform_source/display/drivers/jpu/include/jpgdecv700
PLATFORM := JDecV9200
endif

ifeq ($(es_low_freq),true)
	EXTRA_CFLAGS += -DCONFIG_ES_LOW_FREQ
endif

ifeq ($(CONFIG_JPEGDEC_DEBUG),y)
	EXTRA_CFLAGS += -DJPEGDEC_DEBUG_ENALBE
endif

JPU_SOC_CHIP_NAME := $(strip $(subst ", , $(CONFIG_VENDOR_CHIP_PREFIX)))
JPU_SOC_CHIP_TYPE := $(strip $(subst ", , $(CONFIG_VENDOR_CHIP_SUFFIX)))

ifeq ($(JPU_SOC_CHIP_TYPE), )
EXTRA_CFLAGS += -I$(srctree)/drivers/platform_drivers/ap/platform/$(JPU_SOC_CHIP_NAME)
else
EXTRA_CFLAGS += -I$(srctree)/drivers/platform_drivers/ap/platform/$(JPU_SOC_CHIP_NAME)_$(JPU_SOC_CHIP_TYPE)
endif

ifeq ($(JPU_SOC_CHIP_TYPE), scout)
EXTRA_CFLAGS += -DJPU_SCOUT
endif

ifeq ($(CONFIG_SOC_PG_INFO), y)
EXTRA_CFLAGS += -I$(srctree)/drivers/platform_drivers/ap/platform/common
endif

LOCAL_CFLAGS += -DJPEGD_ACORE_ENALBLE
EXTRA_CFLAGS += -I$(srctree)/lib/libc_sec/securec_v2/include/
EXTRA_CFLAGS += -I$(srctree)/platform_source/display/drivers/jpu/include
EXTRA_CFLAGS += -I$(srctree)/platform_source/display/drivers/jpu/osal/linux
EXTRA_CFLAGS += -I$(srctree)/platform_source/display/drivers/jpu/acore

obj-$(CONFIG_JPU_SUPPORT) := devjpu.o
devjpu-objs := \
	acore/jpu.o \
	acore/jpu_power_manager.o\
	acore/jpu_iommu.o \
	acore/jpu_timer.o \
	osal/linux/linux_osal.o \
	common/jpu_utils.o \
	common/jpu_decode.o \
	common/jpgdec_irq.o

ifneq (,$(filter 700 800 820 840 900 9200,$(CONFIG_DKMD_DPU_VERSION)))
EXTRA_CFLAGS += -I$(srctree)/platform_source/display/drivers/jpu/acore/$(PLATFORM)
devjpu-objs += acore/$(PLATFORM)/jpu_platform.o
endif

ifeq ($(CONFIG_DPU_FB_V600), y)
EXTRA_CFLAGS += -I$(srctree)/platform_source/display/drivers/jpu/acore/JDecV600
devjpu-objs += acore/JDecV600/jpu_platform.o
endif

clean:
	rm *.o .*cmd
