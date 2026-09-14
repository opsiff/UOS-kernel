obj-y += edid_parse.o
obj-y += edid_timing_select.o

EXTRA_CFLAGS += \
	-I$(srctree)/include/ \
	-I$(srctree)/arch/arm64/include/ \
	-I$(srctree)/include/platform_include/display/dkmd/ \
	-I$(srctree)/include/platform_include/display/linux/ \
	-I$(srctree)/drivers/video/fbdev/dkmd/dksm \
	-I$(srctree)/drivers/video/fbdev/dkmd/dksm/peri \
	-I$(srctree)/drivers/video/fbdev/ukmd/uksm \
	-I$(srctree)/drivers/video/fbdev/dkmd/connector \

clean:
	rm -rf *.o .*.cmd *.ko *.mod.c .tmp_versions *.symvers *.order *.symversions signed .*.d
	find . -name "*.o" | xargs rm -f
	find . -name ".*.cmd" | xargs rm -f
	find . -name ".*.d" | xargs rm -f
	find . -name "*.gcno" | xargs rm -f
