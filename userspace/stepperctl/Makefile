BUILD_ROOT ?= $(abspath $(CURDIR)/../../build)
BUILD_DIR := $(BUILD_ROOT)/stepperctl
BIN := $(BUILD_DIR)/stepperctl

CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11
CPPFLAGS += -I$(abspath $(CURDIR)/../../include/uapi)

ifneq ($(strip $(CROSS_COMPILE)),)
CC := $(CROSS_COMPILE)gcc
endif

all: $(BIN)

$(BIN): $(CURDIR)/stepperctl.c $(abspath $(CURDIR)/../../include/uapi/stepper_hat_ioctl.h)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

clean:
	rm -rf $(BUILD_DIR)
