# Makefile - i.MX6ULL 交叉编译

CROSS_COMPILE ?= arm-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
STRIP = $(CROSS_COMPILE)strip

# 目标平台优化 (Cortex-A7)
CFLAGS = -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
CFLAGS += -O2 -ffast-math -funroll-loops
CFLAGS += -Wall -Wextra -Wno-unused-parameter
CFLAGS += -D_GNU_SOURCE -D_REENTRANT
CFLAGS += -I./include -I./drivers/camera -I./core -I./middleware -I./utils

# 库路径 (针对交叉编译环境)
LDFLAGS = -lpthread -lm
LDFLAGS += -lx264 -lavformat -lavcodec -lavutil  # FFmpeg/x264
LDFLAGS += -lyuv  # libyuv (可选，用于格式转换)

TARGET = imx6ull_dvr
BUILD_DIR = build

SRCS = app/main.c \
       app/dvr_daemon.c \
       core/video_pipeline.c \
       core/encoder.c \
       core/recorder.c \
       core/event_handler.c \
       drivers/camera/v4l2_capture.c \
       drivers/display/fb_display.c \
       drivers/storage/sdcard_manager.c \
       drivers/sensors/gsensor.c \
       utils/ring_buffer.c \
       utils/thread_pool.c

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean install

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/app $(BUILD_DIR)/core \
	         $(BUILD_DIR)/drivers/camera \
	         $(BUILD_DIR)/drivers/display \
	         $(BUILD_DIR)/drivers/storage \
	         $(BUILD_DIR)/drivers/sensors \
	         $(BUILD_DIR)/middleware $(BUILD_DIR)/utils

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	$(STRIP) $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

install:
	install -D -m 755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	install -D -m 644 config/dvr.conf $(DESTDIR)/etc/dvr.conf
	install -D -m 755 scripts/dvr.init $(DESTDIR)/etc/init.d/dvr