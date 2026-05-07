/* drivers/camera/v4l2_capture.c */
#include "v4l2_capture.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>

#define LOG_TAG "V4L2"
#include "logger.h"

int v4l2_init(v4l2_handle_t *handle, const video_config_t *cfg)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_streamparm parm;
    enum v4l2_buf_type type;
    
    memset(handle, 0, sizeof(*handle));
    handle->width = cfg->width;
    handle->height = cfg->height;
    handle->fps = cfg->fps;
    handle->pixelformat = cfg->pixelformat;
    pthread_mutex_init(&handle->lock, NULL);

    handle->fd = open(V4L2_DEVICE, O_RDWR | O_NONBLOCK, 0);
    if (handle->fd < 0) {
        LOG_ERR("无法打开 %s: %s", V4L2_DEVICE, strerror(errno));
        return -1;
    }

    /* 1. 查询设备能力 */
    if (ioctl(handle->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        LOG_ERR("VIDIOC_QUERYCAP 失败: %s", strerror(errno));
        goto err_close;
    }
    LOG_INFO("摄像头: %s, 驱动: %s, cap=0x%x", 
             cap.card, cap.driver, cap.capabilities);
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOG_ERR("设备不支持视频采集");
        goto err_close;
    }

    /* 2. 设置视频格式 (OV5640 -> i.MX6ULL CSI) */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = handle->width;
    fmt.fmt.pix.height = handle->height;
    fmt.fmt.pix.pixelformat = handle->pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;        /* 逐行扫描 */
    fmt.fmt.pix.bytesperline = 0;               /* 驱动自动计算 */
    
    if (ioctl(handle->fd, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_ERR("VIDIOC_S_FMT 失败: %s", strerror(errno));
        goto err_close;
    }
    
    /* 驱动可能返回调整后的参数 */
    handle->width = fmt.fmt.pix.width;
    handle->height = fmt.fmt.pix.height;
    LOG_INFO("实际分辨率: %dx%d, 每行 %d bytes, sizeimage=%d",
             handle->width, handle->height,
             fmt.fmt.pix.bytesperline, fmt.fmt.pix.sizeimage);

    /* 3. 设置帧率 (通过 streamparm) */
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = handle->fps;
    
    if (ioctl(handle->fd, VIDIOC_S_PARM, &parm) >= 0) {
        handle->fps = parm.parm.capture.timeperframe.denominator;
        LOG_INFO("实际帧率: %d fps", handle->fps);
    }

    /* 4. 申请 DMA 缓冲区 (mmap 方式，零拷贝) */
    memset(&req, 0, sizeof(req));
    req.count = V4L2_BUF_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(handle->fd, VIDIOC_REQBUFS, &req) < 0) {
        LOG_ERR("VIDIOC_REQBUFS 失败: %s", strerror(errno));
        goto err_close;
    }
    handle->buf_count = req.count;
    LOG_INFO("申请到 %d 个 DMA 缓冲区", handle->buf_count);

    /* 5. 映射缓冲区到用户空间 */
    for (uint32_t i = 0; i < handle->buf_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(handle->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            LOG_ERR("VIDIOC_QUERYBUF (%d) 失败", i);
            goto err_unmap;
        }
        
        handle->buf_ptrs[i] = mmap(NULL, buf.length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED,
                                   handle->fd, buf.m.offset);
        if (handle->buf_ptrs[i] == MAP_FAILED) {
            LOG_ERR("mmap buffer %d 失败", i);
            goto err_unmap;
        }
        handle->buf_infos[i] = buf;
        LOG_DBG("Buffer %d: 内核偏移 0x%x, 映射到 %p, 大小 %d",
                i, buf.m.offset, handle->buf_ptrs[i], buf.length);
    }

    /* 6. 将缓冲区加入采集队列 */
    for (uint32_t i = 0; i < handle->buf_count; i++) {
        struct v4l2_buffer buf = handle->buf_infos[i];
        buf.bytesused = 0;
        if (ioctl(handle->fd, VIDIOC_QBUF, &buf) < 0) {
            LOG_ERR("VIDIOC_QBUF (%d) 失败", i);
            goto err_unmap;
        }
    }

    LOG_INFO("V4L2 初始化完成: %dx%d@%d, FOURCC=%c%c%c%c",
             handle->width, handle->height, handle->fps,
             handle->pixelformat & 0xff,
             (handle->pixelformat >> 8) & 0xff,
             (handle->pixelformat >> 16) & 0xff,
             (handle->pixelformat >> 24) & 0xff);
    return 0;

err_unmap:
    for (uint32_t i = 0; i < handle->buf_count; i++) {
        if (handle->buf_ptrs[i] && handle->buf_ptrs[i] != MAP_FAILED)
            munmap(handle->buf_ptrs[i], handle->buf_infos[i].length);
    }
err_close:
    close(handle->fd);
    handle->fd = -1;
    return -1;
}

int v4l2_start_stream(v4l2_handle_t *handle)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(handle->fd, VIDIOC_STREAMON, &type) < 0) {
        LOG_ERR("VIDIOC_STREAMON 失败: %s", strerror(errno));
        return -1;
    }
    handle->streaming = true;
    LOG_INFO("视频流已启动");
    return 0;
}

void v4l2_stop_stream(v4l2_handle_t *handle)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (handle->streaming) {
        ioctl(handle->fd, VIDIOC_STREAMOFF, &type);
        handle->streaming = false;
        LOG_INFO("视频流已停止");
    }
}

int v4l2_capture_frame(v4l2_handle_t *handle, uint8_t **out_data, uint32_t *out_size)
{
    struct v4l2_buffer buf;
    fd_set fds;
    struct timeval tv;
    int r;
    
    FD_ZERO(&fds);
    FD_SET(handle->fd, &fds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    
    r = select(handle->fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) {
        return -1;  /* 超时或无数据 */
    }
    
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(handle->fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return -1;
        LOG_ERR("VIDIOC_DQBUF 失败: %s", strerror(errno));
        return -1;
    }
    
    *out_data = handle->buf_ptrs[buf.index];
    *out_size = buf.bytesused;
    
    return buf.index;  /* 返回 buffer 索引，用于后续 release */
}

int v4l2_release_buffer(v4l2_handle_t *handle, int buf_idx)
{
    struct v4l2_buffer buf = handle->buf_infos[buf_idx];
    buf.bytesused = 0;
    return ioctl(handle->fd, VIDIOC_QBUF, &buf);
}

void v4l2_deinit(v4l2_handle_t *handle)
{
    v4l2_stop_stream(handle);
    for (uint32_t i = 0; i < handle->buf_count; i++) {
        if (handle->buf_ptrs[i] && handle->buf_ptrs[i] != MAP_FAILED)
            munmap(handle->buf_ptrs[i], handle->buf_infos[i].length);
    }
    if (handle->fd >= 0) {
        close(handle->fd);
        handle->fd = -1;
    }
    pthread_mutex_destroy(&handle->lock);
    LOG_INFO("V4L2 已释放");
}