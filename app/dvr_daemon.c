/* app/dvr_daemon.c */
#include "dvr_daemon.h"
#include "video_pipeline.h"
#include <signal.h>
#include <sys/epoll.h>

#define LOG_TAG "DAEMON"
#include "logger.h"

dvr_context_t g_dvr_ctx = {0};

static int g_epoll_fd = -1;
static volatile bool g_shutdown = false;

static void signal_handler(int sig)
{
    LOG_INFO("收到信号 %d, 准备关机...", sig);
    g_shutdown = true;
    g_dvr_ctx.running = false;
}

static int init_gpio_keys(void)
{
    /* 配置 GPIO 按键中断 (记录键、紧急键) */
    /* 使用 epoll 监听 /sys/class/gpio/gpioXX/value */
    return 0;
}

static int handle_event(dvr_event_t evt)
{
    switch (evt) {
    case DVR_EVT_POWER_ON:
        LOG_INFO("系统上电，自动启动录像");
        if (g_dvr_ctx.config.auto_start) {
            video_pipeline_start();
        }
        break;
        
    case DVR_EVT_KEY_RECORD:
        if (g_dvr_ctx.state == DVR_STATE_RECORDING) {
            video_pipeline_stop();
        } else {
            video_pipeline_start();
        }
        break;
        
    case DVR_EVT_KEY_EMERGENCY:
    case DVR_EVT_COLLISION:
        if (g_dvr_ctx.state == DVR_STATE_RECORDING) {
            g_dvr_ctx.state = DVR_STATE_EMERGENCY;
            recorder_trigger_emergency(g_dvr_ctx.storage.recorder);
        }
        break;
        
    case DVR_EVT_SD_REMOVE:
        LOG_WARN("SD卡拔出! 停止录像");
        video_pipeline_stop();
        g_dvr_ctx.state = DVR_STATE_ERROR;
        break;
        
    case DVR_EVT_SD_INSERT:
        LOG_INFO("SD卡插入");
        /* 重新挂载 */
        break;
        
    case DVR_EVT_SD_FULL:
        /* recorder 内部处理循环覆盖 */
        break;
        
    case DVR_EVT_LOW_BATTERY:
        LOG_WARN("低电量，准备关机");
        g_shutdown = true;
        break;
        
    case DVR_EVT_POWER_OFF:
        g_shutdown = true;
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
    
    /* 读取配置文件 */
    /* load_config(&g_dvr_ctx.config, "/etc/dvr.conf"); */
    
    /* 设置默认值 */
    g_dvr_ctx.config.video.width = DVR_RECOMMEND_WIDTH;
    g_dvr_ctx.config.video.height = DVR_RECOMMEND_HEIGHT;
    g_dvr_ctx.config.video.fps = DVR_RECOMMEND_FPS;
    g_dvr_ctx.config.video.bitrate_kbps = DVR_VIDEO_BITRATE_KBPS;
    g_dvr_ctx.config.video.gop_size = DVR_VIDEO_GOP_SIZE;
    g_dvr_ctx.config.segment_minutes = DVR_SEGMENT_MINUTES;
    g_dvr_ctx.config.record_audio = false;  /* i.MX6ULL CPU 可能无法同时处理音视频 */
    g_dvr_ctx.config.auto_start = true;
    strcpy(g_dvr_ctx.config.storage_path, "/mnt/sdcard/DCIM");
    
    /* 初始化视频流水线 */
    if (video_pipeline_init(&g_dvr_ctx.config) < 0) {
        LOG_ERR("视频流水线初始化失败");
        return -1;
    }
    
    /* 初始化 GPIO/按键 */
    init_gpio_keys();
    
    /* 信号处理 */
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    g_dvr_ctx.running = true;
    return 0;
}

int dvr_daemon_run(void)
{
    LOG_INFO("DVR 守护进程运行中");
    
    /* 上电自动启动 */
    handle_event(DVR_EVT_POWER_ON);
    
    struct epoll_event events[8];
    g_epoll_fd = epoll_create1(0);
    
    /* 添加 GPIO 按键、SD卡检测、定时器等到 epoll */
    
    while (!g_shutdown) {
        int nfds = epoll_wait(g_epoll_fd, events, 8, 1000);
        
        for (int i = 0; i < nfds; i++) {
            /* 处理事件 */
        }
        
        /* 状态机维护 */
        if (g_dvr_ctx.state == DVR_STATE_ERROR) {
            /* 尝试恢复 */
        }
        
        /* 定期检测 SD 卡状态、存储空间 */
    }
    
    return 0;
}

void dvr_daemon_shutdown(void)
{
    LOG_INFO("系统关机，清理资源");
    video_pipeline_stop();
    video_pipeline_deinit();
    pthread_mutex_destroy(&g_dvr_ctx.mutex);
    
    if (g_epoll_fd >= 0) close(g_epoll_fd);
    
    LOG_INFO("DVR 已安全关闭");
}