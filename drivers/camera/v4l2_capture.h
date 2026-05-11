#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include "common.h"

#include <linux/videodev2.h>

#define V4L2_BUF_COUNT 4
#ifndef V4L2_DEVICE
#define V4L2_DEVICE "/dev/video0"
#endif

typedef enum {
    CAM_FMT_UYVY = V4L2_PIX_FMT_UYVY,
    CAM_FMT_YUYV = V4L2_PIX_FMT_YUYV,
    CAM_FMT_RGB565 = V4L2_PIX_FMT_RGB565,
    CAM_FMT_MJPEG = V4L2_PIX_FMT_MJPEG
} camera_format_t;

typedef struct {
    int fd;
    struct v4l2_buffer buf_infos[V4L2_BUF_COUNT];
    uint8_t *buf_ptrs[V4L2_BUF_COUNT];
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t pixelformat;
    uint32_t buf_count;
    bool streaming;
    pthread_mutex_t lock;
} v4l2_handle_t;

int v4l2_init(v4l2_handle_t *handle, const video_config_t *cfg);
void v4l2_deinit(v4l2_handle_t *handle);
int v4l2_start_stream(v4l2_handle_t *handle);
void v4l2_stop_stream(v4l2_handle_t *handle);
int v4l2_capture_frame(v4l2_handle_t *handle, uint8_t **out_data, uint32_t *out_size);
int v4l2_release_buffer(v4l2_handle_t *handle, int buf_idx);
int v4l2_set_exposure(v4l2_handle_t *handle, int value);
int v4l2_query_controls(v4l2_handle_t *handle);

#endif
