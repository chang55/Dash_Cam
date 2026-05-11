#include "recorder.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define LOG_TAG "RECORDER"
#include "logger.h"

typedef struct {
    int fd;
    char filepath[512];
    time_t segment_start;
    uint32_t frame_count;
    uint64_t bytes_written;
    bool has_keyframe;
} segment_ctx_t;

typedef struct {
    uint8_t *data;
    uint32_t size;
    bool is_keyframe;
} prebuffer_frame_t;

struct recorder {
    const dvr_config_t *config;
    encoder_ctx_t *encoder;
    rec_mode_t mode;
    segment_ctx_t current;
    pthread_mutex_t lock;
    prebuffer_frame_t *prebuffer;
    uint32_t prebuffer_capacity;
    uint32_t prebuffer_next;
    uint32_t prebuffer_count;
    time_t emergency_until;
};

static uint64_t get_disk_free_mb(const char *path)
{
    struct statvfs st;
    if (statvfs(path, &st) < 0) return 0;
    return (uint64_t)st.f_bavail * st.f_bsize / (1024 * 1024);
}

static void free_prebuffer_frame(prebuffer_frame_t *frame)
{
    free(frame->data);
    frame->data = NULL;
    frame->size = 0;
    frame->is_keyframe = false;
}

static void prebuffer_push(recorder_t *rec, const uint8_t *data, uint32_t size, bool is_keyframe)
{
    if (!rec->prebuffer || rec->prebuffer_capacity == 0 || !data || size == 0) return;

    prebuffer_frame_t *slot = &rec->prebuffer[rec->prebuffer_next];
    free_prebuffer_frame(slot);
    slot->data = malloc(size);
    if (!slot->data) return;

    memcpy(slot->data, data, size);
    slot->size = size;
    slot->is_keyframe = is_keyframe;

    rec->prebuffer_next = (rec->prebuffer_next + 1) % rec->prebuffer_capacity;
    if (rec->prebuffer_count < rec->prebuffer_capacity) {
        rec->prebuffer_count++;
    }
}

static int write_payload(recorder_t *rec, const uint8_t *data, uint32_t size)
{
    uint32_t total = 0;
    while (total < size) {
        ssize_t written = write(rec->current.fd, data + total, size - total);
        if (written < 0) {
            if (errno == EINTR) continue;
            LOG_WARN("write failed: %s", strerror(errno));
            return -1;
        }
        if (written == 0) {
            return -1;
        }
        total += (uint32_t)written;
    }
    rec->current.bytes_written += total;
    return (int)total;
}

static void write_prebuffer(recorder_t *rec)
{
    if (!rec->prebuffer || rec->prebuffer_count == 0 || rec->current.fd < 0) return;

    uint32_t start = (rec->prebuffer_next + rec->prebuffer_capacity - rec->prebuffer_count) %
                     rec->prebuffer_capacity;
    uint32_t first = 0;
    for (; first < rec->prebuffer_count; first++) {
        uint32_t idx = (start + first) % rec->prebuffer_capacity;
        if (rec->prebuffer[idx].is_keyframe) break;
    }

    for (uint32_t i = first; i < rec->prebuffer_count; i++) {
        uint32_t idx = (start + i) % rec->prebuffer_capacity;
        if (rec->prebuffer[idx].data && rec->prebuffer[idx].size > 0) {
            write_payload(rec, rec->prebuffer[idx].data, rec->prebuffer[idx].size);
        }
    }
}

static void cleanup_old_files(recorder_t *rec)
{
    if (get_disk_free_mb(rec->config->storage_path) >= DVR_RESERVED_SPACE_MB) {
        return;
    }

    DIR *dir = opendir(rec->config->storage_path);
    if (!dir) return;

    struct dirent *entry;
    char oldest_file[512] = {0};
    time_t oldest_time = time(NULL);

    while ((entry = readdir(dir)) != NULL) {
        if (!strstr(entry->d_name, ".h264")) continue;
        if (strstr(entry->d_name, "_EMERGENCY_")) continue;

        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", rec->config->storage_path, entry->d_name);

        struct stat st;
        if (stat(fullpath, &st) == 0 && st.st_mtime < oldest_time) {
            oldest_time = st.st_mtime;
            snprintf(oldest_file, sizeof(oldest_file), "%s", fullpath);
        }
    }
    closedir(dir);

    if (oldest_file[0]) {
        LOG_INFO("remove old clip: %s", oldest_file);
        unlink(oldest_file);
    }
}

static int open_new_segment(recorder_t *rec)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    snprintf(rec->current.filepath, sizeof(rec->current.filepath),
             "%s/DVR_%04d%02d%02d_%02d%02d%02d_%s.h264",
             rec->config->storage_path,
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec,
             rec->mode == REC_MODE_EMERGENCY ? "EMERGENCY" : "NORMAL");

    rec->current.fd = open(rec->current.filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (rec->current.fd < 0) {
        LOG_ERR("create clip failed: %s: %s", rec->current.filepath, strerror(errno));
        return -1;
    }

    rec->current.segment_start = now;
    rec->current.frame_count = 0;
    rec->current.bytes_written = 0;
    rec->current.has_keyframe = false;
    LOG_INFO("new clip: %s", rec->current.filepath);
    return 0;
}

recorder_t *recorder_create(const dvr_config_t *config, encoder_ctx_t *encoder)
{
    recorder_t *rec = calloc(1, sizeof(*rec));
    if (!rec) return NULL;

    rec->config = config;
    rec->encoder = encoder;
    rec->current.fd = -1;
    pthread_mutex_init(&rec->lock, NULL);

    uint32_t fps = config->video.fps ? config->video.fps : DVR_RECOMMEND_FPS;
    rec->prebuffer_capacity = config->emergency_pre_sec * fps;
    if (rec->prebuffer_capacity > 0) {
        rec->prebuffer = calloc(rec->prebuffer_capacity, sizeof(*rec->prebuffer));
    }

    if (mkdir(config->storage_path, 0755) < 0 && errno != EEXIST) {
        LOG_WARN("mkdir %s failed: %s", config->storage_path, strerror(errno));
    }
    return rec;
}

int recorder_start(recorder_t *rec, rec_mode_t mode)
{
    if (!rec) return -1;
    pthread_mutex_lock(&rec->lock);

    if (rec->current.fd >= 0) {
        close(rec->current.fd);
        rec->current.fd = -1;
    }

    rec->mode = mode;
    if (mode == REC_MODE_EMERGENCY) {
        rec->emergency_until = time(NULL) + rec->config->emergency_post_sec;
    }
    int ret = open_new_segment(rec);
    if (ret == 0 && mode == REC_MODE_EMERGENCY) {
        write_prebuffer(rec);
    }

    pthread_mutex_unlock(&rec->lock);
    return ret;
}

void recorder_stop(recorder_t *rec)
{
    if (!rec) return;
    pthread_mutex_lock(&rec->lock);
    if (rec->current.fd >= 0) {
        LOG_INFO("close clip: %s, frames=%u, bytes=%llu",
                 rec->current.filepath, rec->current.frame_count,
                 (unsigned long long)rec->current.bytes_written);
        close(rec->current.fd);
        rec->current.fd = -1;
    }
    pthread_mutex_unlock(&rec->lock);
}

int recorder_write_frame(recorder_t *rec, uint8_t *h264_data, uint32_t h264_size,
                         bool is_keyframe, const gps_data_t *gps)
{
    (void)gps;
    if (!rec || !h264_data || h264_size == 0) return -1;

    pthread_mutex_lock(&rec->lock);
    if (rec->current.fd < 0) {
        pthread_mutex_unlock(&rec->lock);
        return -1;
    }

    time_t now = time(NULL);
    if (rec->mode == REC_MODE_EMERGENCY && rec->emergency_until > 0 && now >= rec->emergency_until) {
        close(rec->current.fd);
        rec->current.fd = -1;
        rec->mode = REC_MODE_NORMAL;
        if (open_new_segment(rec) < 0) {
            pthread_mutex_unlock(&rec->lock);
            return -1;
        }
    }

    if ((now - rec->current.segment_start) >= (time_t)(rec->config->segment_minutes * 60) &&
        is_keyframe) {
        close(rec->current.fd);
        rec->current.fd = -1;
        cleanup_old_files(rec);
        if (open_new_segment(rec) < 0) {
            pthread_mutex_unlock(&rec->lock);
            return -1;
        }
    }

    if (rec->mode != REC_MODE_EMERGENCY &&
        get_disk_free_mb(rec->config->storage_path) < DVR_RESERVED_SPACE_MB) {
        cleanup_old_files(rec);
    }

    int ret = write_payload(rec, h264_data, h264_size);
    if (ret >= 0) {
        rec->current.frame_count++;
        rec->current.has_keyframe = rec->current.has_keyframe || is_keyframe;
        if (rec->mode == REC_MODE_NORMAL) {
            prebuffer_push(rec, h264_data, h264_size, is_keyframe);
        }
    }

    pthread_mutex_unlock(&rec->lock);
    return ret;
}

int recorder_force_new_segment(recorder_t *rec)
{
    if (!rec) return -1;
    pthread_mutex_lock(&rec->lock);
    if (rec->current.fd >= 0) {
        close(rec->current.fd);
        rec->current.fd = -1;
    }
    int ret = open_new_segment(rec);
    pthread_mutex_unlock(&rec->lock);
    return ret;
}

int recorder_trigger_emergency(recorder_t *rec)
{
    if (!rec) return -1;
    pthread_mutex_lock(&rec->lock);

    if (rec->current.fd >= 0) {
        close(rec->current.fd);
        rec->current.fd = -1;
    }
    rec->mode = REC_MODE_EMERGENCY;
    rec->emergency_until = time(NULL) + rec->config->emergency_post_sec;
    int ret = open_new_segment(rec);
    if (ret == 0) {
        write_prebuffer(rec);
    }

    pthread_mutex_unlock(&rec->lock);
    return ret;
}

bool recorder_is_emergency(recorder_t *rec)
{
    if (!rec) return false;
    pthread_mutex_lock(&rec->lock);
    bool ret = rec->mode == REC_MODE_EMERGENCY;
    pthread_mutex_unlock(&rec->lock);
    return ret;
}

void recorder_get_current_clip_info(recorder_t *rec, clip_info_t *info)
{
    if (!rec || !info) return;

    pthread_mutex_lock(&rec->lock);
    memset(info, 0, sizeof(*info));
    snprintf(info->filename, sizeof(info->filename), "%s", rec->current.filepath);
    info->start_time = rec->current.segment_start;
    info->end_time = time(NULL);
    info->duration_sec = (uint32_t)(info->end_time - info->start_time);
    info->file_size = rec->current.bytes_written;
    info->is_emergency = rec->mode == REC_MODE_EMERGENCY;
    pthread_mutex_unlock(&rec->lock);
}

void recorder_destroy(recorder_t *rec)
{
    if (!rec) return;

    recorder_stop(rec);
    for (uint32_t i = 0; i < rec->prebuffer_capacity; i++) {
        free_prebuffer_frame(&rec->prebuffer[i]);
    }
    free(rec->prebuffer);
    pthread_mutex_destroy(&rec->lock);
    free(rec);
}
