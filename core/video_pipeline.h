#ifndef DVR_VIDEO_PIPELINE_H
#define DVR_VIDEO_PIPELINE_H

#include "common.h"

int video_pipeline_init(const dvr_config_t *config);
void video_pipeline_deinit(void);
int video_pipeline_start(void);
void video_pipeline_stop(void);
int video_pipeline_trigger_emergency(void);
int video_pipeline_get_stats(char *buf, size_t bufsize);

#endif
