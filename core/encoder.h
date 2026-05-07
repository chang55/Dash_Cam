/* core/encoder.h */
#ifndef ENCODER_H
#define ENCODER_H

#include "common.h"

/* 编码器类型 */
typedef enum {
    ENC_X264_CBR,       /* x264 CBR 模式 */
    ENC_X264_VBR,       /* x264 VBR 模式 (质量优先) */
    ENC_FFMPEG_SW       /* FFmpeg 纯软件编码器 */
} encoder_type_t;

typedef struct encoder_ctx encoder_ctx_t;

struct encoder_ctx {
    encoder_type_t  type;
    uint32_t        width;
    uint32_t        height;
    uint32_t        fps;
    uint32_t        bitrate_kbps;
    uint32_t        gop_size;
    
    /* 编码器内部状态 (不透明) */
    void           *priv_data;      /* x264_t* 或 AVCodecContext* */
    void           *pic_in;         /* 输入图像缓冲 */
    void           *pic_out;        /* 输出图像缓冲 */
    uint64_t        frame_count;
    uint64_t        pts;
    
    /* 统计 */
    uint32_t        encode_time_ms;  /* 每帧编码耗时 */
    float           fps_actual;
};

encoder_ctx_t* encoder_create(const video_config_t *cfg, encoder_type_t type);
void           encoder_destroy(encoder_ctx_t *enc);
int            encoder_encode_frame(encoder_ctx_t *enc, 
                                   uint8_t *yuv_data, uint32_t yuv_size,
                                   uint8_t **h264_out, uint32_t *h264_size,
                                   bool *is_keyframe);
int            encoder_force_keyframe(encoder_ctx_t *enc);

#endif
