/* core/encoder.c - 使用 x264 编码器 (针对 i.MX6ULL 优化) */
#include "encoder.h"
#include <x264.h>
#include <time.h>

#define LOG_TAG "ENCODER"
#include "logger.h"

encoder_ctx_t* encoder_create(const video_config_t *cfg, encoder_type_t type)
{
    encoder_ctx_t *enc = calloc(1, sizeof(encoder_ctx_t));
    if (!enc) return NULL;
    
    enc->type = type;
    enc->width = cfg->width;
    enc->height = cfg->height;
    enc->fps = cfg->fps;
    enc->bitrate_kbps = cfg->bitrate_kbps;
    enc->gop_size = cfg->gop_size;
    
    x264_param_t param;
    x264_picture_t *pic_in = calloc(1, sizeof(x264_picture_t));
    x264_picture_t *pic_out = calloc(1, sizeof(x264_picture_t));
    
    /* 针对 i.MX6ULL (单核 A7, 无 NEON 向量优化限制) 的优化参数 */
    x264_param_default_preset(&param, "ultrafast", "zerolatency");
    
    param.i_width = enc->width;
    param.i_height = enc->height;
    param.i_fps_num = enc->fps;
    param.i_fps_den = 1;
    param.i_keyint_max = enc->gop_size;
    param.i_keyint_min = enc->gop_size / 2;
    
    /* 码率控制 - CBR 更适合行车记录仪 */
    param.rc.i_rc_method = X264_RC_ABR;
    param.rc.i_bitrate = enc->bitrate_kbps;
    param.rc.i_vbv_buffer_size = enc->bitrate_kbps;
    param.rc.i_vbv_max_bitrate = enc->bitrate_kbps * 1.2;
    
    /* 针对嵌入式 CPU 的极致优化 */
    param.i_threads = 1;              /* 单线程 (单核 CPU) */
    param.b_sliced_threads = 0;
    param.i_lookahead_threads = 0;    /* 禁用 lookahead 降低延迟 */
    param.rc.i_lookahead = 0;         /* 零延迟 */
    param.i_sync_lookahead = 0;
    param.b_vfr_input = 0;
    param.b_repeat_headers = 1;       /* 每个关键帧前面带 SPS/PPS */
    param.b_annexb = 1;               /* Annex-B 格式 (H.264) */
    param.i_level_idc = 30;           /* Level 3.0 (720p@25fps) */
    
    /* 降低 CPU 负载的编码选项 */
    param.analyse.i_me_method = X264_ME_DIA;      /* 菱形搜索 (最快) */
    param.analyse.i_subpel_refine = 1;            /* 最低质量子像素 */
    param.i_frame_reference = 1;                  /* 单参考帧 */
    param.analyse.b_mixed_references = 0;
    param.analyse.i_trellis = 0;                  /* 禁用 Trellis */
    param.b_deblocking_filter = 1;
    param.psz_tune = "fastdecode";               /* 快速解码友好 */
    
    /* 对于 UYVY 输入，需要转为 I420 */
    x264_picture_alloc(pic_in, X264_CSP_I420, enc->width, enc->height);
    
    x264_t *h = x264_encoder_open(&param);
    if (!h) {
        LOG_ERR("x264_encoder_open 失败");
        free(pic_in);
        free(pic_out);
        free(enc);
        return NULL;
    }
    
    enc->priv_data = h;
    enc->pic_in = pic_in;
    enc->pic_out = pic_out;
    
    LOG_INFO("x264 编码器创建: %dx%d@%d, %dkbps, GOP=%d, preset=ultrafast", 
             enc->width, enc->height, enc->fps, enc->bitrate_kbps, enc->gop_size);
    return enc;
}

int encoder_encode_frame(encoder_ctx_t *enc,
                         uint8_t *yuv_data, uint32_t yuv_size,
                         uint8_t **h264_out, uint32_t *h264_size,
                         bool *is_keyframe)
{
    x264_t *h = enc->priv_data;
    x264_picture_t *pic_in = enc->pic_in;
    x264_picture_t *pic_out = enc->pic_out;
    x264_nal_t *nals = NULL;
    int nal_count = 0;
    
    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    
    /* UYVY -> I420 转换 (简单实现，可优化为 ARM NEON) */
    if (enc->type == ENC_X264_CBR || enc->type == ENC_X264_VBR) {
        uint8_t *y_plane = pic_in->img.plane[0];
        uint8_t *u_plane = pic_in->img.plane[1];
        uint8_t *v_plane = pic_in->img.plane[2];
        
        /* 这里需要 UYVY 到 I420 的转换，或直接从 V4L2 请求 I420 格式 */
        /* 简化示意 - 实际实现需要像素格式转换 */
        uint32_t pixel_count = enc->width * enc->height;
        /* ... 格式转换代码 (可使用 libyuv 或自研 NEON 版本) ... */
        (void)yuv_data; (void)yuv_size; /* 占位 */
    }
    
    pic_in->i_pts = enc->pts++;
    
    int frame_size = x264_encoder_encode(h, &nals, &nal_count, pic_in, pic_out);
    
    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    enc->encode_time_ms = (ts_end.tv_sec - ts_start.tv_sec) * 1000 +
                          (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000;
    
    if (frame_size > 0 && nals) {
        *h264_out = nals[0].p_payload;
        *h264_size = frame_size;
        *is_keyframe = (pic_out->i_type == X264_TYPE_IDR || 
                        pic_out->i_type == X264_TYPE_I);
        enc->frame_count++;
        enc->fps_actual = 1000.0f / enc->encode_time_ms;
        
        /* 性能警告：单核 A7 编码 720p 可能接近极限 */
        if (enc->encode_time_ms > (1000 / enc->fps)) {
            LOG_WARN("编码耗时 %dms 超过帧间隔 %dms，可能掉帧!",
                     enc->encode_time_ms, 1000 / enc->fps);
        }
        return 0;
    }
    return -1;  /* 无输出帧或错误 */
}

int encoder_force_keyframe(encoder_ctx_t *enc)
{
    x264_picture_t *pic_in = enc->pic_in;
    pic_in->i_type = X264_TYPE_IDR;
    pic_in->i_qpplus1 = X264_QP_AUTO;
    return 0;
}

void encoder_destroy(encoder_ctx_t *enc)
{
    if (!enc) return;
    x264_t *h = enc->priv_data;
    if (h) {
        x264_encoder_close(h);
    }
    if (enc->pic_in) {
        x264_picture_clean(enc->pic_in);
        free(enc->pic_in);
    }
    if (enc->pic_out) {
        x264_picture_clean(enc->pic_out);
        free(enc->pic_out);
    }
    free(enc);
    LOG_INFO("编码器已销毁, 共编码 %llu 帧", enc->frame_count);
}