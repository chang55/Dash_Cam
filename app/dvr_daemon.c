#include "dvr_daemon.h"

#include "common.h"
#include "config_manager.h"
#include "gsensor.h"
#include "sdcard_manager.h"
#include "video_pipeline.h"

#include <signal.h>
#include <sys/epoll.h>

#define LOG_TAG "DAEMON"
#include "logger.h"

dvr_context_t g_dvr_ctx;

static int g_epoll_fd = -1;
static volatile sig_atomic_t g_shutdown = 0;

static int handle_event(dvr_event_t evt);

static void signal_handler(int sig)
{
    LOG_INFO("received signal %d, shutting down", sig);
    g_shutdown = 1;
    g_dvr_ctx.running = false;
}

static int init_gpio_keys(void)
{
    /* GPIO key interrupt integration is platform-specific and will be added later. */
    return 0;
}

static void poll_storage_status(void)
{
    storage_info_t info;
    if (sdcard_query(g_dvr_ctx.config.storage_path, &info) == 0) {
        g_dvr_ctx.storage = info;
    } else {
        g_dvr_ctx.storage.mounted = false;
    }
}

static void poll_gsensor_status(void)
{
    gsensor_data_t data;
    if (gsensor_read(&data) == 0 && data.collision_detected) {
        handle_event(DVR_EVT_COLLISION);
    }
}

static int handle_event(dvr_event_t evt)
{
    switch (evt) {
    case DVR_EVT_POWER_ON:
        LOG_INFO("power on");
        if (g_dvr_ctx.config.auto_start) {
            return video_pipeline_start();
        }
        break;
    case DVR_EVT_KEY_RECORD:
        if (g_dvr_ctx.state == DVR_STATE_RECORDING || g_dvr_ctx.state == DVR_STATE_EMERGENCY) {
            video_pipeline_stop();
        } else {
            return video_pipeline_start();
        }
        break;
    case DVR_EVT_KEY_EMERGENCY:
    case DVR_EVT_COLLISION:
        if (g_dvr_ctx.state == DVR_STATE_RECORDING || g_dvr_ctx.state == DVR_STATE_EMERGENCY) {
            return video_pipeline_trigger_emergency();
        }
        break;
    case DVR_EVT_SD_REMOVE:
        LOG_WARN("SD card removed, stopping recording");
        video_pipeline_stop();
        g_dvr_ctx.state = DVR_STATE_ERROR;
        break;
    case DVR_EVT_SD_INSERT:
        LOG_INFO("SD card inserted");
        break;
    case DVR_EVT_LOW_BATTERY:
    case DVR_EVT_POWER_OFF:
        g_shutdown = 1;
        break;
    default:
        break;
    }

    return 0;
}

int dvr_daemon_init(void)
{
    memset(&g_dvr_ctx, 0, sizeof(g_dvr_ctx));
    pthread_mutex_init(&g_dvr_ctx.mutex, NULL);

    dvr_config_set_defaults(&g_dvr_ctx.config);
    if (dvr_config_load_file(&g_dvr_ctx.config, "/etc/dvr.conf") == 0) {
        LOG_INFO("loaded config from /etc/dvr.conf");
    } else {
        LOG_INFO("using built-in default config");
    }

    if (video_pipeline_init(&g_dvr_ctx.config) < 0) {
        LOG_ERR("video pipeline init failed");
        pthread_mutex_destroy(&g_dvr_ctx.mutex);
        return -1;
    }

    init_gpio_keys();
    gsensor_init();
    poll_storage_status();
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    g_dvr_ctx.state = DVR_STATE_IDLE;
    g_dvr_ctx.running = true;
    g_dvr_ctx.start_time = time(NULL);
    return 0;
}

int dvr_daemon_run(void)
{
    LOG_INFO("DVR daemon running");
    handle_event(DVR_EVT_POWER_ON);

    g_epoll_fd = epoll_create1(0);
    if (g_epoll_fd < 0) {
        LOG_WARN("epoll_create1 failed: %s", strerror(errno));
    }

    while (!g_shutdown) {
        if (g_epoll_fd >= 0) {
            struct epoll_event events[8];
            int nfds = epoll_wait(g_epoll_fd, events, 8, 1000);
            if (nfds < 0 && errno != EINTR) {
                LOG_WARN("epoll_wait failed: %s", strerror(errno));
            }
        } else {
            sleep(1);
        }

        poll_storage_status();
        poll_gsensor_status();
    }

    return 0;
}

void dvr_daemon_shutdown(void)
{
    LOG_INFO("DVR daemon shutdown");
    g_dvr_ctx.state = DVR_STATE_SHUTDOWN;
    video_pipeline_stop();
    video_pipeline_deinit();
    gsensor_deinit();

    if (g_epoll_fd >= 0) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }

    pthread_mutex_destroy(&g_dvr_ctx.mutex);
}
