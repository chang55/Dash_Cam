#ifndef DVR_SDCARD_MANAGER_H
#define DVR_SDCARD_MANAGER_H

#include "common.h"

int sdcard_query(const char *path, storage_info_t *info);
bool sdcard_is_mounted(const char *mount_path);

#endif
