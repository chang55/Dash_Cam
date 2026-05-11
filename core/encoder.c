#include "encoder.h"

#include <linux/videodev2.h>
#include <time.h>
#include <x264.h>

#define LOG_TAG "ENCODER"
#include "logger.h"

static void uyvy_to_i420(const uint8_t *src, uint32_t width, uint32_t height,
                         uint8_t *y_plane, uint8_t *u_plane, uint8_t *v_plane)
{
    for (uint32_t row = 0; row < height; row++) {
        const uint8_t *s = src + row * width * 2;
        uint8_t *y = y_plane + row * width;

        for (uint32_t col = 0; col < width; col += 2) {
            uint8_t u = s[0];
            uint8_t y0 = s[1];
            uint8_t v = s[2];
            uint8_t y1 = s[3];

            y[col] = y0;
            if (col + 1 < width) y[col + 1] = y1;

            if ((row & 1) == 0) {
                uint32_t uv_idx = (row / 2) * (width / 2) + (col / 2);
                u_plane[uv_idx] = u;
                v_plane[uv_idx] = v;
            }
            s += 4;
        }
    }
}

static void yuyv_to_i420(const uint8_t *src, uint32_t width, uint32_t height,
                         uint8_t *y_plane, uint8_t *u_plane, uint8_t *v_plane)
{
    for (uint32_t row = 0; row < height; row++) {
        const uint8_t *s = src + row * width * 2;
        uint8_t *y = y_plane + row * width;

        for (uint32_t col = 0; col < width; col += 2) {
            uint8_t y0 = s[0];
            uint8_t u = s[1];
            uint8_t y1 = s[2];
            uint8_t v = s[3];

            y[col] = y0;
            if (col + 1 < width) y[col + 1] = y1;

            if ((row & 1) == 0) {
                uint32_t uv_idx = (row / 2) * (width / 2) + (col / 2);
                u_plane[uv_idx] = u;
                v_plane[uv_idx] = v;
            }
            s += 4;
        }
    }
}

encoder_ctx_t *encoder_create(const video_config_t *cfg, encoder_type_t type)
{
    encoder_ctx_t *enc = calloc(1, sizeof(*enc));
    if (!enc) return NULL;

    enc->type = type;
    enc->width = cfg->width;
    enc->height = cfg->height;
    enc->fps = cfg->fps;
    enc->bitrate_kbps = cfg->bitrate_kbps;
    enc->gop_size = cfg->gop_size;
    enc->pixelformat = cfg->pixelformat;

    x264_param_t param;
    x264_param_default_preset(&param, "ultrafast", "zerolatency");
    param.i_width = (int)enc->width;
    param.i_height = (int)enc->height;
    param.i_fps_num = (int)enc->fps;
    param.i_fps_den = 1;
    param.i_keyint_max = (int)enc->gop_size;
    param.i_keyint_min = (int)(enc->gop_size / 2);
    param.rc.i_rc_method = X264_RC_ABR;
    param.rc.i_bitrate = (int)enc->bitrate_kbps;
    param.rc.i_vbv_buffer_size = (int)enc->bitrate_kbps;
    param.rc.i_vbv_max_bitrate = (int)(enc->bitrate_kbps * 12 / 10);
    param.i_threads = 1;
    param.b_sliced_threads = 0;
    param.rc.i_lookahead = 0;
    param.i_sync_lookahead = 0;
    param.b_vfr_input = 0;
    param.b_repeat_headers = 1;
    param.b_annexb = 1;
    param.i_level_idc = 30;
    param.analyse.i_me_method = X264_ME_DIA;
    param.analyse.i_subpel_refine = 1;
    param.i_frame_reference = 1;
    param.analyse.b_mixed_references = 0;
    param.analyse.i_trellis = 0;
    param.b_deblocking_filter = 1;

    x264_picture_t *pic_in = calloc(1, sizeof(*pic_in));
    x264_picture_t *pic_out = calloc(1, sizeof(*pic_out));
    if (!pic_in || !pic_out) {
        free(pic_in);
        free(pic_out);
        free(enc);
        return NULL;
    }

    if (x264_picture_alloc(pic_in, X264_CSP_I420, (int)enc->width, (int)enc->height) < 0) {
        free(pic_in);
        free(pic_out);
        free(enc);
        return NULL;
    }

    x264_t *h = x264_encoder_open(&param);
    if (!h) {
        x264_picture_clean(pic_in);
        free(pic_in);
        free(pic_out);
        free(enc);
        return NULL;
    }

    enc->priv_data = h;
    enc->pic_in = pic_in;
    enc->pic_out = pic_out;
    LOG_INFO("x264 encoder ready: %ux%u@%u %ukbps", enc->width, enc->height, enc->fps, enc->bitrate_kbps);
    return enc;
}

int encoder_encode_frame(encoder_ctx_t *enc,
                         uint8_t *yuv_data, uint32_t yuv_size,
                         uint8_t **h264_out, uint32_t *h264_size,
                         bool *is_keyframe)
{
    if (!enc || !yuv_data || !h264_out || !h264_size || !is_keyframe) {
        return -1;
    }

    uint32_t expected_yuv422 = enc->width * enc->height * 2;
    if (yuv_size < expected_yuv422) {
        LOG_WARN("input frame too small: got %u, expected at least %u", yuv_size, expected_yuv422);
        return -1;
    }

    x264_t *h = enc->priv_data;
    x264_picture_t *pic_in = enc->pic_in;
    x264_picture_t *pic_out = enc->pic_out;
    x264_nal_t *nals = NULL;
    int nal_count = 0;
    struct timespec ts_start;
    struct timespec ts_end;

    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    if (enc->pixelformat == V4L2_PIX_FMT_UYVY) {
        uyvy_to_i420(yuv_data, enc->width, enc->height,
                     pic_in->img.plane[0], pic_in->img.plane[1], pic_in->img.plane[2]);
    } else if (enc->pixelformat == V4L2_PIX_FMT_YUYV) {
        yuyv_to_i420(yuv_data, enc->width, enc->height,
                     pic_in->img.plane[0], pic_in->img.plane[1], pic_in->img.plane[2]);
    } else {
        LOG_WARN("unsupported pixel format 0x%x", enc->pixelformat);
        return -1;
    }

    pic_in->i_pts = (int64_t)enc->pts++;
    int frame_size = x264_encoder_encode(h, &nals, &nal_count, pic_in, pic_out);
    pic_in->i_type = X264_TYPE_AUTO;

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    enc->encode_time_ms = (uint32_t)((ts_end.tv_sec - ts_start.tv_sec) * 1000 +
                                     (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000);

    if (frame_size <= 0 || !nals || nal_count <= 0) {
        return -1;
    }

    *h264_out = nals[0].p_payload;
    *h264_size = (uint32_t)frame_size;
    *is_keyframe = pic_out->i_type == X264_TYPE_IDR || pic_out->i_type == X264_TYPE_I;
    enc->frame_count++;
    enc->fps_actual = enc->encode_time_ms > 0 ? 1000.0f / enc->encode_time_ms : 0.0f;

    if (enc->encode_time_ms > (1000 / enc->fps)) {
        LOG_WARN("slow encode: %ums exceeds frame interval %ums",
                 enc->encode_time_ms, 1000 / enc->fps);
    }
    return 0;
}

int encoder_force_keyframe(encoder_ctx_t *enc)
{
    if (!enc || !enc->pic_in) return -1;
    x264_picture_t *pic_in = enc->pic_in;
    pic_in->i_type = X264_TYPE_IDR;
    return 0;
}

void encoder_destroy(encoder_ctx_t *enc)
{
    if (!enc) return;

    if (enc->priv_data) {
        x264_encoder_close((x264_t *)enc->priv_data);
    }
    if (enc->pic_in) {
        x264_picture_clean((x264_picture_t *)enc->pic_in);
        free(enc->pic_in);
    }
    free(enc->pic_out);
    LOG_INFO("encoder destroyed, frames=%llu", (unsigned long long)enc->frame_count);
    free(enc);
}
