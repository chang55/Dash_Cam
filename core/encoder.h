#ifndef DVR_ENCODER_H
#define DVR_ENCODER_H

#include "common.h"

typedef enum {
    ENC_X264_CBR = 0,
    ENC_X264_VBR,
    ENC_FFMPEG_SW
} encoder_type_t;

typedef struct encoder_ctx encoder_ctx_t;

struct encoder_ctx {
    encoder_type_t type;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t bitrate_kbps;
    uint32_t gop_size;
    uint32_t pixelformat;
    void *priv_data;
    void *pic_in;
    void *pic_out;
    uint64_t frame_count;
    uint64_t pts;
    uint32_t encode_time_ms;
    float fps_actual;
};

encoder_ctx_t *encoder_create(const video_config_t *cfg, encoder_type_t type);
void encoder_destroy(encoder_ctx_t *enc);
int encoder_encode_frame(encoder_ctx_t *enc,
                         uint8_t *yuv_data, uint32_t yuv_size,
                         uint8_t **h264_out, uint32_t *h264_size,
                         bool *is_keyframe);
int encoder_force_keyframe(encoder_ctx_t *enc);

#endif
