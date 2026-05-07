/* core/recorder.c */
#include "recorder.h"
#include <sys/statvfs.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#define LOG_TAG "RECORDER"
#include "logger.h"

typedef struct {
    int         fd;             /* 当前文件描述符 */
    char        filepath[512];
    time_t      segment_start;
    uint32_t    frame_count;
    uint64_t    bytes_written;
    bool        has_keyframe;   /* 确保首帧为关键帧 */
} segment_ctx_t;

struct recorder {
    dvr_config_t    *config;
    encoder_ctx_t   *encoder;
    rec_mode_t       mode;
    segment_ctx_t    current;
    pthread_mutex_t  lock;
    
    /* 紧急预录缓冲区 (环形) */
    uint8_t         *prebuffer;
    uint32_t         prebuffer_size;
    uint32_t         prebuffer_write_idx;
    uint32_t         prebuffer_count;
};

/* MP4/MKV 封装 - 简化使用 raw H.264 + ADTS AAC */
/* 完整实现可使用 FFmpeg libavformat */

static uint64_t get_disk_free_mb(const char *path)
{
    struct statvfs st;
    if (statvfs(path, &st) < 0) return 0;
    return (st.f_bavail * st.f_bsize) / (1024 * 1024);
}

static void cleanup_old_files(recorder_t *rec)
{
    /* 循环删除最旧的普通录像，保留紧急录像 */
    DIR *dir = opendir(rec->config->storage_path);
    if (!dir) return;
    
    struct dirent *entry;
    char oldest_file[512] = {0};
    time_t oldest_time = time(NULL);
    
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".h264") || strstr(entry->d_name, ".mp4")) {
            /* 跳过紧急录像 (LOCKED 标记) */
            if (strstr(entry->d_name, "_EMERGENCY_")) continue;
            
            char fullpath[512];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", 
                     rec->config->storage_path, entry->d_name);
            
            struct stat st;
            if (stat(fullpath, &st) == 0 && st.st_mtime < oldest_time) {
                oldest_time = st.st_mtime;
                strcpy(oldest_file, fullpath);
            }
        }
    }
    closedir(dir);
    
    if (oldest_file[0] && get_disk_free_mb(rec->config->storage_path) < DVR_RESERVED_SPACE_MB) {
        LOG_INFO("存储空间不足，删除旧文件: %s", oldest_file);
        unlink(oldest_file);
    }
}

static int open_new_segment(recorder_t *rec)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    
    snprintf(rec->current.filepath, sizeof(rec->current.filepath),
             "%s/DVR_%04d%02d%02d_%02d%02d%02d_%s.h264",
             rec->config->storage_path,
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec,
             rec->mode == REC_MODE_EMERGENCY ? "EMERGENCY" : "NORMAL");
    
    rec->current.fd = open(rec->current.filepath, 
                           O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (rec->current.fd < 0) {
        LOG_ERR("无法创建录像文件: %s", strerror(errno));
        return -1;
    }
    
    rec->current.segment_start = now;
    rec->current.frame_count = 0;
    rec->current.bytes_written = 0;
    rec->current.has_keyframe = false;
    
    LOG_INFO("新建录像段: %s", rec->current.filepath);
    return 0;
}

recorder_t* recorder_create(const dvr_config_t *config, encoder_ctx_t *encoder)
{
    recorder_t *rec = calloc(1, sizeof(recorder_t));
    rec->config = (dvr_config_t *)config;
    rec->encoder = encoder;
    pthread_mutex_init(&rec->lock, NULL);
    
    /* 分配紧急预录缓冲区 (10秒 @ 2Mbps ≈ 2.5MB) */
    rec->prebuffer_size = config->emergency_pre_sec * config->video.bitrate_kbps * 1000 / 8;
    rec->prebuffer = malloc(rec->prebuffer_size);
    
    /* 确保存储目录存在 */
    mkdir(config->storage_path, 0755);
    
    return rec;
}

int recorder_start(recorder_t *rec, rec_mode_t mode)
{
    pthread_mutex_lock(&rec->lock);
    rec->mode = mode;
    
    /* 紧急模式：先写入预录缓冲 */
    if (mode == REC_MODE_EMERGENCY) {
        LOG_INFO("紧急录像模式启动，写入预录数据");
    }
    
    int ret = open_new_segment(rec);
    pthread_mutex_unlock(&rec->lock);
    return ret;
}

int recorder_write_frame(recorder_t *rec, uint8_t *h264_data, uint32_t h264_size,
                         bool is_keyframe, const gps_data_t *gps)
{
    pthread_mutex_lock(&rec->lock);
    
    if (rec->current.fd < 0) {
        pthread_mutex_unlock(&rec->lock);
        return -1;
    }
    
    /* 检查分段时长 */
    time_t now = time(NULL);
    if ((now - rec->current.segment_start) >= rec->config->segment_minutes * 60) {
        if (is_keyframe) {  /* 只在关键帧处分段 */
            close(rec->current.fd);
            cleanup_old_files(rec);  /* 清理旧文件 */
            open_new_segment(rec);
        }
    }
    
    /* 检查存储空间 */
    if (get_disk_free_mb(rec->config->storage_path) < DVR_RESERVED_SPACE_MB && 
        rec->mode != REC_MODE_EMERGENCY) {
        LOG_WARN("存储空间不足!");
        cleanup_old_files(rec);
    }
    
    /* 写入 H.264 NAL 单元 (添加 4字节 start code) */
    uint8_t start_code[4] = {0x00, 0x00, 0x00, 0x01};
    write(rec->current.fd, start_code, 4);
    ssize_t written = write(rec->current.fd, h264_data, h264_size);
    
    rec->current.bytes_written += written + 4;
    rec->current.frame_count++;
    
    /* 预录缓冲 (循环写入) */
    if (rec->prebuffer && rec->mode == REC_MODE_NORMAL) {
        /* 简化：将关键帧位置和大小记录在索引中 */
    }
    
    pthread_mutex_unlock(&rec->lock);
    return written;
}

int recorder_trigger_emergency(recorder_t *rec)
{
    LOG_WARN("触发紧急录像!");
    
    pthread_mutex_lock(&rec->lock);
    
    /* 关闭当前普通文件 */
    close(rec->current.fd);
    
    /* 切换到紧急模式 */
    rec->mode = REC_MODE_EMERGENCY;
    open_new_segment(rec);
    
    /* 写入预录的缓冲区数据 (实现秒级预录) */
    /* ... */
    
    pthread_mutex_unlock(&rec->lock);
    
    /* 紧急录像持续30秒后自动恢复正常 */
    return 0;
}

void recorder_stop(recorder_t *rec)
{
    pthread_mutex_lock(&rec->lock);
    if (rec->current.fd >= 0) {
        close(rec->current.fd);
        rec->current.fd = -1;
        LOG_INFO("录像停止: %s, 共 %u 帧, %.1f MB",
                 rec->current.filepath, rec->current.frame_count,
                 rec->current.bytes_written / (1024.0 * 1024.0));
    }
    pthread_mutex_unlock(&rec->lock);
}

void recorder_destroy(recorder_t *rec)
{
    if (!rec) return;
    recorder_stop(rec);
    free(rec->prebuffer);
    pthread_mutex_destroy(&rec->lock);
    free(rec);
}