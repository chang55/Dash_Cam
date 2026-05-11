#include "gsensor.h"

int gsensor_init(void)
{
    return 0;
}

void gsensor_deinit(void)
{
}

int gsensor_read(gsensor_data_t *data)
{
    if (!data) return -1;
    memset(data, 0, sizeof(*data));
    return 0;
}
