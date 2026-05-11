#include "video_pipeline.h"

#include "encoder.h"
#include "frame_queue.h"
#include "recorder.h"
#include "v4l2_capture.h"

#define LOG_TAG "PIPELINE"
#include "logger.h"

#define FRAME_QUEUE_CAPACITY 4

static struct {
    v4l2_handle_t camera;
    encoder_ctx_t *encoder;
    recorder_t *recorder;
    frame_queue_t queue;
    pthread_t capture_tid;
    pthread_t encode_tid;
    bool initialized;
    bool running;
    bool capture_thread_started;
    bool encode_thread_started;
    uint64_t captured_frames;
    uint64_t encoded_frames;
    uint64_t dropped_frames;
    pthread_mutex_t lock;
} g_pipeline;

extern dvr_context_t g_dvr_ctx;

static void *capture_thread(void *arg)
{
    (void)arg;

    while (g_pipeline.running) {
        uint8_t *frame_data = NULL;
        uint32_t frame_size = 0;

        int buf_idx = v4l2_capture_frame(&g_pipeline.camera, &frame_data, &frame_size);
        if (buf_idx < 0) continue;

        uint64_t seq = ++g_pipeline.captured_frames;
        if (frame_queue_push(&g_pipeline.queue, frame_data, frame_size, seq) < 0) {
            g_pipeline.dropped_frames++;
        }
        v4l2_release_buffer(&g_pipeline.camera, buf_idx);
    }

    return NULL;
}

static void *encode_thread(void *arg)
{
    (void)arg;

    while (g_pipeline.running) {
        frame_packet_t packet;
        memset(&packet, 0, sizeof(packet));

        if (frame_queue_pop(&g_pipeline.queue, &packet) < 0) {
            continue;
        }

        uint8_t *h264_data = NULL;
        uint32_t h264_size = 0;
        bool is_keyframe = false;

        if (encoder_encode_frame(g_pipeline.encoder, packet.data, packet.size,
                                 &h264_data, &h264_size, &is_keyframe) == 0) {
            if (g_dvr_ctx.state == DVR_STATE_RECORDING ||
                g_dvr_ctx.state == DVR_STATE_EMERGENCY) {
                recorder_write_frame(g_pipeline.recorder, h264_data, h264_size, is_keyframe, NULL);
                if (g_dvr_ctx.state == DVR_STATE_EMERGENCY &&
                    !recorder_is_emergency(g_pipeline.recorder)) {
                    g_dvr_ctx.state = DVR_STATE_RECORDING;
                    g_dvr_ctx.emergency_flag = false;
                }
            }
            g_pipeline.encoded_frames++;
        } else {
            g_pipeline.dropped_frames++;
        }

        frame_queue_packet_release(&packet);
    }

    return NULL;
}

int video_pipeline_init(const dvr_config_t *config)
{
    memset(&g_pipeline, 0, sizeof(g_pipeline));
    pthread_mutex_init(&g_pipeline.lock, NULL);

    video_config_t vcfg = config->video;
    if (vcfg.pixelformat == 0) {
        vcfg.pixelformat = V4L2_PIX_FMT_UYVY;
    }

    if (frame_queue_init(&g_pipeline.queue, FRAME_QUEUE_CAPACITY) < 0) {
        LOG_ERR("frame queue init failed");
        goto err_lock;
    }

    if (v4l2_init(&g_pipeline.camera, &vcfg) < 0) {
        goto err_queue;
    }

    vcfg.width = g_pipeline.camera.width;
    vcfg.height = g_pipeline.camera.height;
    vcfg.fps = g_pipeline.camera.fps;
    vcfg.pixelformat = g_pipeline.camera.pixelformat;

    g_pipeline.encoder = encoder_create(&vcfg, ENC_X264_CBR);
    if (!g_pipeline.encoder) {
        LOG_ERR("encoder create failed");
        goto err_camera;
    }

    g_pipeline.recorder = recorder_create(config, g_pipeline.encoder);
    if (!g_pipeline.recorder) {
        LOG_ERR("recorder create failed");
        goto err_encoder;
    }

    g_pipeline.initialized = true;
    return 0;

err_encoder:
    encoder_destroy(g_pipeline.encoder);
    g_pipeline.encoder = NULL;
err_camera:
    v4l2_deinit(&g_pipeline.camera);
err_queue:
    frame_queue_destroy(&g_pipeline.queue);
err_lock:
    pthread_mutex_destroy(&g_pipeline.lock);
    return -1;
}

int video_pipeline_start(void)
{
    pthread_mutex_lock(&g_pipeline.lock);
    if (!g_pipeline.initialized) {
        pthread_mutex_unlock(&g_pipeline.lock);
        return -1;
    }
    if (g_pipeline.running) {
        pthread_mutex_unlock(&g_pipeline.lock);
        return 0;
    }

    frame_queue_reset(&g_pipeline.queue);
    if (recorder_start(g_pipeline.recorder, REC_MODE_NORMAL) < 0) {
        pthread_mutex_unlock(&g_pipeline.lock);
        return -1;
    }
    if (v4l2_start_stream(&g_pipeline.camera) < 0) {
        recorder_stop(g_pipeline.recorder);
        pthread_mutex_unlock(&g_pipeline.lock);
        return -1;
    }

    g_pipeline.running = true;
    if (pthread_create(&g_pipeline.capture_tid, NULL, capture_thread, NULL) != 0) {
        g_pipeline.running = false;
        v4l2_stop_stream(&g_pipeline.camera);
        recorder_stop(g_pipeline.recorder);
        pthread_mutex_unlock(&g_pipeline.lock);
        return -1;
    }
    g_pipeline.capture_thread_started = true;

    if (pthread_create(&g_pipeline.encode_tid, NULL, encode_thread, NULL) != 0) {
        g_pipeline.running = false;
        frame_queue_stop(&g_pipeline.queue);
        pthread_join(g_pipeline.capture_tid, NULL);
        g_pipeline.capture_thread_started = false;
        v4l2_stop_stream(&g_pipeline.camera);
        recorder_stop(g_pipeline.recorder);
        pthread_mutex_unlock(&g_pipeline.lock);
        return -1;
    }
    g_pipeline.encode_thread_started = true;

    g_dvr_ctx.state = DVR_STATE_RECORDING;
    pthread_mutex_unlock(&g_pipeline.lock);
    LOG_INFO("pipeline started");
    return 0;
}

void video_pipeline_stop(void)
{
    pthread_mutex_lock(&g_pipeline.lock);
    if (!g_pipeline.running) {
        pthread_mutex_unlock(&g_pipeline.lock);
        return;
    }

    g_pipeline.running = false;
    frame_queue_stop(&g_pipeline.queue);
    pthread_mutex_unlock(&g_pipeline.lock);

    if (g_pipeline.capture_thread_started) {
        pthread_join(g_pipeline.capture_tid, NULL);
        g_pipeline.capture_thread_started = false;
    }
    if (g_pipeline.encode_thread_started) {
        pthread_join(g_pipeline.encode_tid, NULL);
        g_pipeline.encode_thread_started = false;
    }

    v4l2_stop_stream(&g_pipeline.camera);
    recorder_stop(g_pipeline.recorder);
    g_dvr_ctx.state = DVR_STATE_IDLE;
    LOG_INFO("pipeline stopped");
}

int video_pipeline_trigger_emergency(void)
{
    if (!g_pipeline.running || !g_pipeline.recorder) {
        return -1;
    }

    if (recorder_trigger_emergency(g_pipeline.recorder) < 0) {
        return -1;
    }
    encoder_force_keyframe(g_pipeline.encoder);
    g_dvr_ctx.state = DVR_STATE_EMERGENCY;
    g_dvr_ctx.emergency_flag = true;
    return 0;
}

void video_pipeline_deinit(void)
{
    if (!g_pipeline.initialized) return;

    video_pipeline_stop();
    recorder_destroy(g_pipeline.recorder);
    encoder_destroy(g_pipeline.encoder);
    v4l2_deinit(&g_pipeline.camera);
    frame_queue_destroy(&g_pipeline.queue);
    pthread_mutex_destroy(&g_pipeline.lock);
    memset(&g_pipeline, 0, sizeof(g_pipeline));
}

int video_pipeline_get_stats(char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0) return -1;

    int n = snprintf(buf, bufsize,
                     "running=%d captured=%llu encoded=%llu dropped=%llu",
                     g_pipeline.running ? 1 : 0,
                     (unsigned long long)g_pipeline.captured_frames,
                     (unsigned long long)g_pipeline.encoded_frames,
                     (unsigned long long)g_pipeline.dropped_frames);
    return n < 0 || (size_t)n >= bufsize ? -1 : 0;
}
