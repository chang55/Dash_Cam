#ifndef DVR_RECORDER_H
#define DVR_RECORDER_H

#include "common.h"
#include "encoder.h"

typedef struct recorder recorder_t;

typedef enum {
    REC_MODE_NORMAL = 0,
    REC_MODE_EMERGENCY,
    REC_MODE_MANUAL
} rec_mode_t;

typedef struct {
    char filename[256];
    time_t start_time;
    time_t end_time;
    uint32_t duration_sec;
    uint64_t file_size;
    bool is_emergency;
    bool has_gps_data;
} clip_info_t;

recorder_t *recorder_create(const dvr_config_t *config, encoder_ctx_t *encoder);
void recorder_destroy(recorder_t *rec);
int recorder_start(recorder_t *rec, rec_mode_t mode);
void recorder_stop(recorder_t *rec);
int recorder_write_frame(recorder_t *rec,
                         uint8_t *h264_data, uint32_t h264_size,
                         bool is_keyframe,
                         const gps_data_t *gps);
int recorder_force_new_segment(recorder_t *rec);
int recorder_trigger_emergency(recorder_t *rec);
bool recorder_is_emergency(recorder_t *rec);
void recorder_get_current_clip_info(recorder_t *rec, clip_info_t *info);

#endif
