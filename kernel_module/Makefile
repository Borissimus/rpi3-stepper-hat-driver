# Copyright (c) Borys Nykytiuk <borysworking@gmail.com>

obj-m += stepper_hat.o

KDIR := /usr/src/linux-headers-6.12.47+rpt-rpi-v8
BUILD_ROOT ?= $(abspath $(CURDIR)/../build)
STAGE_DIR := $(BUILD_ROOT)/kernel_module
UAPI_STAGE_DIR := $(BUILD_ROOT)/include/uapi
MODULE_FILES := Makefile stepper_hat.c
UAPI_HEADER := $(abspath $(CURDIR)/../include/uapi/stepper_hat_ioctl.h)

all:
	mkdir -p $(STAGE_DIR)
	mkdir -p $(UAPI_STAGE_DIR)
	cp $(addprefix $(CURDIR)/,$(MODULE_FILES)) $(STAGE_DIR)/
	cp $(UAPI_HEADER) $(UAPI_STAGE_DIR)/
	make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -C $(KDIR) M=$(STAGE_DIR) modules

clean:
	rm -rf $(STAGE_DIR) $(BUILD_ROOT)/include
