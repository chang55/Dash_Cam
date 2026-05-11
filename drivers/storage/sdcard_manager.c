#include "sdcard_manager.h"

#include <sys/statvfs.h>

bool sdcard_is_mounted(const char *mount_path)
{
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) return false;

    char dev[128];
    char mnt[128];
    char fs[64];
    bool found = false;
    while (fscanf(fp, "%127s %127s %63s %*s %*d %*d\n", dev, mnt, fs) == 3) {
        (void)dev;
        (void)fs;
        if (strcmp(mnt, mount_path) == 0) {
            found = true;
            break;
        }
    }

    fclose(fp);
    return found;
}

int sdcard_query(const char *path, storage_info_t *info)
{
    if (!path || !info) return -1;

    struct statvfs st;
    memset(info, 0, sizeof(*info));
    snprintf(info->mount_path, sizeof(info->mount_path), "%s", path);

    if (statvfs(path, &st) < 0) {
        info->mounted = false;
        return -1;
    }

    info->total_bytes = (uint64_t)st.f_blocks * st.f_frsize;
    info->free_bytes = (uint64_t)st.f_bavail * st.f_frsize;
    info->mounted = true;
    info->readonly = (st.f_flag & ST_RDONLY) != 0;
    return 0;
}
