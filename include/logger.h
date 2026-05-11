#ifndef DVR_LOGGER_H
#define DVR_LOGGER_H

#include <stdio.h>
#include <time.h>

#ifndef LOG_TAG
#define LOG_TAG "DVR"
#endif

static inline const char *dvr_log_time(void)
{
    static __thread char buf[24];
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    return buf;
}

#define LOG_PRINT(level, fmt, ...) \
    fprintf(stderr, "%s [%s] %s: " fmt "\n", dvr_log_time(), level, LOG_TAG, ##__VA_ARGS__)

#define LOG_ERR(fmt, ...)  LOG_PRINT("ERROR", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_PRINT("WARN", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_PRINT("INFO", fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...)  LOG_PRINT("DEBUG", fmt, ##__VA_ARGS__)

#endif
