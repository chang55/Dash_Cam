#ifndef DVR_COMMON_H
#define DVR_COMMON_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DVR_VERSION_MAJOR   1
#define DVR_VERSION_MINOR   0
#define DVR_VERSION_PATCH   0
#define DVR_VERSION_STR     "1.0.0"

#define DVR_CPU_MHZ             792
#define DVR_MAX_ENCODE_WIDTH    1280
#define DVR_MAX_ENCODE_HEIGHT   720
#define DVR_MAX_FPS             25
#define DVR_RECOMMEND_WIDTH     848
#define DVR_RECOMMEND_HEIGHT    480
#define DVR_RECOMMEND_FPS       20

#define DVR_VIDEO_BITRATE_KBPS  2048
#define DVR_VIDEO_GOP_SIZE      50
#define DVR_SEGMENT_MINUTES     3
#define DVR_RESERVED_SPACE_MB   512
#define DVR_EMERGENCY_PRE_SEC   10
#define DVR_EMERGENCY_POST_SEC  30

typedef enum {
    DVR_STATE_IDLE = 0,
    DVR_STATE_PREVIEW,
    DVR_STATE_RECORDING,
    DVR_STATE_EMERGENCY,
    DVR_STATE_ERROR,
    DVR_STATE_SHUTDOWN
} dvr_state_t;

typedef enum {
    DVR_EVT_POWER_ON = 0,
    DVR_EVT_POWER_OFF,
    DVR_EVT_KEY_RECORD,
    DVR_EVT_KEY_EMERGENCY,
    DVR_EVT_KEY_MODE,
    DVR_EVT_COLLISION,
    DVR_EVT_SD_INSERT,
    DVR_EVT_SD_REMOVE,
    DVR_EVT_SD_FULL,
    DVR_EVT_OVERHEAT,
    DVR_EVT_LOW_BATTERY
} dvr_event_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t bitrate_kbps;
    uint32_t gop_size;
    uint32_t pixelformat;
} video_config_t;

typedef struct {
    char mount_path[128];
    uint64_t total_bytes;
    uint64_t free_bytes;
    bool mounted;
    bool readonly;
    int write_speed_mbps;
} storage_info_t;

typedef struct {
    double latitude;
    double longitude;
    double speed_kmh;
    double heading;
    time_t timestamp;
    bool valid;
} gps_data_t;

typedef struct {
    float accel_x;
    float accel_y;
    float accel_z;
    float impact_force;
    bool collision_detected;
} gsensor_data_t;

typedef struct {
    video_config_t video;
    uint32_t segment_minutes;
    uint32_t emergency_pre_sec;
    uint32_t emergency_post_sec;
    bool record_audio;
    bool gps_overlay;
    bool auto_start;
    char storage_path[128];
} dvr_config_t;

typedef struct {
    dvr_state_t state;
    dvr_config_t config;
    storage_info_t storage;
    pthread_mutex_t mutex;
    bool running;
    bool emergency_flag;
    time_t start_time;
} dvr_context_t;

extern dvr_context_t g_dvr_ctx;

#define SAFE_MUTEX_LOCK(m)   pthread_mutex_lock(m)
#define SAFE_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)

#ifdef __cplusplus
}
#endif

#endif
