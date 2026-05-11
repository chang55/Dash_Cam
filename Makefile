# Makefile for the embedded DVR prototype.

CROSS_COMPILE ?= arm-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip

TARGET := imx6ull_dvr
BUILD_DIR := build

SRCS := \
	app/main.c \
	app/dvr_daemon.c \
	core/config_manager.c \
	core/video_pipeline.c \
	core/encoder.c \
	core/recorder.c \
	drivers/camera/v4l2_capture.c \
	drivers/storage/sdcard_manager.c \
	drivers/sensors/gsensor.c \
	utils/frame_queue.c

OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

CFLAGS ?= -O2
CFLAGS += -std=gnu11 -Wall -Wextra -Werror=implicit-function-declaration
CFLAGS += -D_GNU_SOURCE -D_REENTRANT
CFLAGS += -I./include -I./app -I./core -I./drivers/camera -I./drivers/storage -I./drivers/sensors -I./utils

TARGET_CFLAGS ?= -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
CFLAGS += $(TARGET_CFLAGS)

LDLIBS += -lpthread -lm -lx264

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDLIBS)
	-$(STRIP) $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	install -D -m 644 config/dvr.conf $(DESTDIR)/etc/dvr.conf

-include $(DEPS)
