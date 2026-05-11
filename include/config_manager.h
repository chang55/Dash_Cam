#ifndef DVR_CONFIG_MANAGER_H
#define DVR_CONFIG_MANAGER_H

#include "common.h"

void dvr_config_set_defaults(dvr_config_t *config);
int dvr_config_load_file(dvr_config_t *config, const char *path);

#endif
