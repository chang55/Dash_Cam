#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 版本信息 ==================== */
#define DVR_VERSION_MAJOR   1
#define DVR_VERSION_MINOR   0
#define DVR_VERSION_PATCH   0
#define DVR_VERSION_STR     "1.0.0"

/* ==================== 硬件限制 (i.MX6ULL) ==================== */
#define DVR_CPU_MHZ             792          /* 典型运行频率 */
#define DVR_MAX_ENCODE_WIDTH    1280         /* 最大编码宽度 */
#define DVR_MAX_ENCODE_HEIGHT   720          /* 最大编码高度 (720p) */
#define DVR_MAX_FPS             25           /* 软编码最大帧率 */
#define DVR_RECOMMEND_WIDTH     848          /* 推荐分辨率 848x480 */
#define DVR_RECOMMEND_HEIGHT    480
#define DVR_RECOMMEND_FPS       20

/* ==================== 视频参数 ==================== */
#define DVR_VIDEO_BITRATE_KBPS  2048         /* 默认码率 2Mbps */
#define DVR_VIDEO_GOP_SIZE      50           /* GOP 长度 */
#define DVR_SEGMENT_MINUTES     3            /* 分段录制时长(分钟) */
#define DVR_MAX_STORAGE_GB      32           /* 最大支持存储容量 */
#define DVR_RESERVED_SPACE_MB   512          /* 保留空间(MB) */
#define DVR_EMERGENCY_PRE_SEC   10           /* 紧急录像预录秒数 */
#define DVR_EMERGENCY_POST_SEC  30           /* 紧急录像后录秒数 */

/* ==================== 状态枚举 ==================== */
typedef enum {
    DVR_STATE_IDLE = 0,          /* 空闲/待机 */
    DVR_STATE_PREVIEW,           /* 实时预览中 */
    DVR_STATE_RECORDING,         /* 录像中 */
    DVR_STATE_EMERGENCY,         /* 紧急录像锁定 */
    DVR_STATE_ERROR,             /* 错误状态 */
    DVR_STATE_SHUTDOWN           /* 正在关机 */
} dvr_state_t;

typedef enum {
    DVR_EVT_POWER_ON = 0,
    DVR_EVT_POWER_OFF,
    DVR_EVT_KEY_RECORD,          /* 手动录像键 */
    DVR_EVT_KEY_EMERGENCY,       /* 紧急锁定键 */
    DVR_EVT_KEY_MODE,            /* 模式切换键 */
    DVR_EVT_COLLISION,           /* 碰撞检测事件 */
    DVR_EVT_SD_INSERT,           /* SD卡插入 */
    DVR_EVT_SD_REMOVE,           /* SD卡拔出 */
    DVR_EVT_SD_FULL,             /* 存储空间满 */
    DVR_EVT_OVERHEAT,            /* 过热警告 */
    DVR_EVT_LOW_BATTERY          /* 低电量 */
} dvr_event_t;

/* ==================== 数据结构 ==================== */
typedef struct {
    uint32_t    width;
    uint32_t    height;
    uint32_t    fps;
    uint32_t    bitrate_kbps;
    uint32_t    gop_size;
    uint32_t    pixelformat;     /* V4L2_PIX_FMT_xxx */
} video_config_t;

typedef struct {
    char        mount_path[128];     /* 挂载点如 /mnt/sdcard */
    uint64_t    total_bytes;
    uint64_t    free_bytes;
    bool        mounted;
    bool        readonly;
    int         write_speed_mbps;    /* 写入速度测试 */
} storage_info_t;

typedef struct {
    double      latitude;
    double      longitude;
    double      speed_kmh;
    double      heading;
    time_t      timestamp;
    bool        valid;
} gps_data_t;

typedef struct {
    float       accel_x;         /* X轴加速度 (g) */
    float       accel_y;
    float       accel_z;
    float       impact_force;    /* 冲击力 */
    bool        collision_detected;
} gsensor_data_t;

typedef struct {
    video_config_t  video;
    uint32_t        segment_minutes;
    uint32_t        emergency_pre_sec;
    uint32_t        emergency_post_sec;
    bool            record_audio;
    bool            gps_overlay;
    bool            auto_start;
    char            storage_path[128];
} dvr_config_t;

/* ==================== 全局上下文 ==================== */
typedef struct {
    dvr_state_t     state;
    dvr_config_t    config;
    storage_info_t  storage;
    pthread_mutex_t mutex;
    pthread_t       record_tid;
    pthread_t       preview_tid;
    pthread_t       event_tid;
    bool            running;
    bool            emergency_flag;
    time_t          start_time;
} dvr_context_t;

extern dvr_context_t g_dvr_ctx;

#define SAFE_MUTEX_LOCK(m)  pthread_mutex_lock(m)
#define SAFE_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)

#ifdef __cplusplus
}
#endif

#endif /* COMMON_H */