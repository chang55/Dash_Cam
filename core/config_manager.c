#include "config_manager.h"

#include <ctype.h>

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

void dvr_config_set_defaults(dvr_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->video.width = DVR_RECOMMEND_WIDTH;
    config->video.height = DVR_RECOMMEND_HEIGHT;
    config->video.fps = DVR_RECOMMEND_FPS;
    config->video.bitrate_kbps = DVR_VIDEO_BITRATE_KBPS;
    config->video.gop_size = DVR_VIDEO_GOP_SIZE;
    config->segment_minutes = DVR_SEGMENT_MINUTES;
    config->emergency_pre_sec = DVR_EMERGENCY_PRE_SEC;
    config->emergency_post_sec = DVR_EMERGENCY_POST_SEC;
    config->record_audio = false;
    config->gps_overlay = false;
    config->auto_start = true;
    snprintf(config->storage_path, sizeof(config->storage_path), "%s", "/mnt/sdcard/DCIM");
}

int dvr_config_load_file(dvr_config_t *config, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char *p = trim(line);
        if (*p == '\0' || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = trim(p);
        char *value = trim(eq + 1);

        if (strcmp(key, "width") == 0) config->video.width = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "height") == 0) config->video.height = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "fps") == 0) config->video.fps = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "bitrate_kbps") == 0) config->video.bitrate_kbps = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "gop_size") == 0) config->video.gop_size = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "segment_minutes") == 0) config->segment_minutes = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "emergency_pre_sec") == 0) config->emergency_pre_sec = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "emergency_post_sec") == 0) config->emergency_post_sec = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "record_audio") == 0) config->record_audio = atoi(value) != 0;
        else if (strcmp(key, "gps_overlay") == 0) config->gps_overlay = atoi(value) != 0;
        else if (strcmp(key, "auto_start") == 0) config->auto_start = atoi(value) != 0;
        else if (strcmp(key, "storage_path") == 0) {
            snprintf(config->storage_path, sizeof(config->storage_path), "%s", value);
        }
    }

    fclose(fp);
    return 0;
}
