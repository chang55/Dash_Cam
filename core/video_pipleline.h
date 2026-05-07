/* core/video_pipeline.h */
#ifndef VIDEO_PIPELINE_H
#define VIDEO_PIPELINE_H

#include "common.h"
#include "v4l2_capture.h"
#include "encoder.h"
#include "recorder.h"

int  video_pipeline_init(const dvr_config_t *config);
void video_pipeline_deinit(void);
int  video_pipeline_start(void);
void video_pipeline_stop(void);
int  video_pipeline_get_stats(char *buf, size_t bufsize);

#endif