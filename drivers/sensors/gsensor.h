#ifndef DVR_GSENSOR_H
#define DVR_GSENSOR_H

#include "common.h"

int gsensor_init(void);
void gsensor_deinit(void);
int gsensor_read(gsensor_data_t *data);

#endif
