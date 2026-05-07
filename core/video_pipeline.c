/* core/video_pipeline.c */
#include "video_pipeline.h"

#define LOG_TAG "PIPELINE"
#include "logger.h"

static struct {
    v4l2_handle_t   camera;
    encoder_ctx_t  *encoder;
    recorder_t     *recorder;
    pthread_t       capture_tid;
    pthread_t       encode_tid;
    bool            running;
    /* 帧队列 (生产者-消费者) */
} g_pipeline;

static void* capture_thread(void *arg)
{
    while (g_pipeline.running) {
        uint8_t *frame_data = NULL;
        uint32_t frame_size = 0;
        
        int buf_idx = v4l2_capture_frame(&g_pipeline.camera, &frame_data, &frame_size);
        if (buf_idx < 0) continue;
        
        /* 提交到编码队列 (零拷贝：只传指针和 buffer index) */
        /* 实际实现使用 ring_buffer */
        
        v4l2_release_buffer(&g_pipeline.camera, buf_idx);
    }
    return NULL;
}

static void* encode_thread(void *arg)
{
    while (g_pipeline.running) {
        /* 从队列取出 YUV 帧 */
        uint8_t *yuv_data = NULL;  /* ... */
        uint32_t yuv_size = 0;
        
        uint8_t *h264_data = NULL;
        uint32_t h264_size = 0;
        bool is_keyframe = false;
        
        if (encoder_encode_frame(g_pipeline.encoder, yuv_data, yuv_size,
                                  &h264_data, &h264_size, &is_keyframe) == 0) {
            /* 写入录像 */
            if (g_dvr_ctx.state == DVR_STATE_RECORDING || 
                g_dvr_ctx.state == DVR_STATE_EMERGENCY) {
                recorder_write_frame(g_pipeline.recorder, h264_data, h264_size,
                                     is_keyframe, NULL);
            }
        }
    }
    return NULL;
}

int video_pipeline_init(const dvr_config_t *config)
{
    video_config_t vcfg = {
        .width = config->video.width,
        .height = config->video.height,
        .fps = config->video.fps,
        .bitrate_kbps = config->video.bitrate_kbps,
        .gop_size = config->video.gop_size,
        .pixelformat = V4L2_PIX_FMT_UYVY  /* 或 V4L2_PIX_FMT_YUV420 */
    };
    
    if (v4l2_init(&g_pipeline.camera, &vcfg) < 0) return -1;
    
    g_pipeline.encoder = encoder_create(&vcfg, ENC_X264_CBR);
    if (!g_pipeline.encoder) {
        v4l2_deinit(&g_pipeline.camera);
        return -1;
    }
    
    g_pipeline.recorder = recorder_create(config, g_pipeline.encoder);
    
    return 0;
}

int video_pipeline_start(void)
{
    g_pipeline.running = true;
    v4l2_start_stream(&g_pipeline.camera);
    pthread_create(&g_pipeline.capture_tid, NULL, capture_thread, NULL);
    pthread_create(&g_pipeline.encode_tid, NULL, encode_thread, NULL);
    g_dvr_ctx.state = DVR_STATE_RECORDING;
    return 0;
}

void video_pipeline_stop(void)
{
    g_pipeline.running = false;
    g_dvr_ctx.state = DVR_STATE_IDLE;
    pthread_join(g_pipeline.capture_tid, NULL);
    pthread_join(g_pipeline.encode_tid, NULL);
    v4l2_stop_stream(&g_pipeline.camera);
}