#include "v4l2_capture.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>

#define LOG_TAG "V4L2"
#include "logger.h"

int v4l2_init(v4l2_handle_t *handle, const video_config_t *cfg)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_streamparm parm;

    memset(handle, 0, sizeof(*handle));
    handle->fd = -1;
    handle->width = cfg->width;
    handle->height = cfg->height;
    handle->fps = cfg->fps;
    handle->pixelformat = cfg->pixelformat;
    pthread_mutex_init(&handle->lock, NULL);

    handle->fd = open(V4L2_DEVICE, O_RDWR | O_NONBLOCK);
    if (handle->fd < 0) {
        LOG_ERR("open %s failed: %s", V4L2_DEVICE, strerror(errno));
        goto err_destroy_lock;
    }

    if (ioctl(handle->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        LOG_ERR("VIDIOC_QUERYCAP failed: %s", strerror(errno));
        goto err_close;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOG_ERR("%s does not support video capture", V4L2_DEVICE);
        goto err_close;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = handle->width;
    fmt.fmt.pix.height = handle->height;
    fmt.fmt.pix.pixelformat = handle->pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(handle->fd, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_ERR("VIDIOC_S_FMT failed: %s", strerror(errno));
        goto err_close;
    }

    handle->width = fmt.fmt.pix.width;
    handle->height = fmt.fmt.pix.height;
    handle->pixelformat = fmt.fmt.pix.pixelformat;

    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = handle->fps;
    if (ioctl(handle->fd, VIDIOC_S_PARM, &parm) == 0 &&
        parm.parm.capture.timeperframe.denominator > 0) {
        handle->fps = parm.parm.capture.timeperframe.denominator;
    }

    memset(&req, 0, sizeof(req));
    req.count = V4L2_BUF_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(handle->fd, VIDIOC_REQBUFS, &req) < 0 || req.count == 0) {
        LOG_ERR("VIDIOC_REQBUFS failed: %s", strerror(errno));
        goto err_close;
    }
    handle->buf_count = req.count > V4L2_BUF_COUNT ? V4L2_BUF_COUNT : req.count;

    for (uint32_t i = 0; i < handle->buf_count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(handle->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            LOG_ERR("VIDIOC_QUERYBUF %u failed: %s", i, strerror(errno));
            goto err_unmap;
        }

        handle->buf_ptrs[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, handle->fd, buf.m.offset);
        if (handle->buf_ptrs[i] == MAP_FAILED) {
            handle->buf_ptrs[i] = NULL;
            LOG_ERR("mmap buffer %u failed: %s", i, strerror(errno));
            goto err_unmap;
        }
        handle->buf_infos[i] = buf;
    }

    for (uint32_t i = 0; i < handle->buf_count; i++) {
        struct v4l2_buffer buf = handle->buf_infos[i];
        if (ioctl(handle->fd, VIDIOC_QBUF, &buf) < 0) {
            LOG_ERR("VIDIOC_QBUF %u failed: %s", i, strerror(errno));
            goto err_unmap;
        }
    }

    LOG_INFO("camera ready: %ux%u@%u fourcc=%c%c%c%c",
             handle->width, handle->height, handle->fps,
             handle->pixelformat & 0xff,
             (handle->pixelformat >> 8) & 0xff,
             (handle->pixelformat >> 16) & 0xff,
             (handle->pixelformat >> 24) & 0xff);
    return 0;

err_unmap:
    for (uint32_t i = 0; i < handle->buf_count; i++) {
        if (handle->buf_ptrs[i]) {
            munmap(handle->buf_ptrs[i], handle->buf_infos[i].length);
            handle->buf_ptrs[i] = NULL;
        }
    }
err_close:
    close(handle->fd);
    handle->fd = -1;
err_destroy_lock:
    pthread_mutex_destroy(&handle->lock);
    return -1;
}

int v4l2_start_stream(v4l2_handle_t *handle)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(handle->fd, VIDIOC_STREAMON, &type) < 0) {
        LOG_ERR("VIDIOC_STREAMON failed: %s", strerror(errno));
        return -1;
    }
    handle->streaming = true;
    return 0;
}

void v4l2_stop_stream(v4l2_handle_t *handle)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (handle->fd >= 0 && handle->streaming) {
        ioctl(handle->fd, VIDIOC_STREAMOFF, &type);
        handle->streaming = false;
    }
}

int v4l2_capture_frame(v4l2_handle_t *handle, uint8_t **out_data, uint32_t *out_size)
{
    fd_set fds;
    struct timeval tv;
    struct v4l2_buffer buf;

    FD_ZERO(&fds);
    FD_SET(handle->fd, &fds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    int ret = select(handle->fd + 1, &fds, NULL, NULL, &tv);
    if (ret <= 0) {
        return -1;
    }

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(handle->fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno != EAGAIN) {
            LOG_WARN("VIDIOC_DQBUF failed: %s", strerror(errno));
        }
        return -1;
    }

    *out_data = handle->buf_ptrs[buf.index];
    *out_size = buf.bytesused;
    return (int)buf.index;
}

int v4l2_release_buffer(v4l2_handle_t *handle, int buf_idx)
{
    if (buf_idx < 0 || (uint32_t)buf_idx >= handle->buf_count) {
        return -1;
    }

    struct v4l2_buffer buf = handle->buf_infos[buf_idx];
    return ioctl(handle->fd, VIDIOC_QBUF, &buf);
}

void v4l2_deinit(v4l2_handle_t *handle)
{
    if (!handle) return;

    v4l2_stop_stream(handle);
    for (uint32_t i = 0; i < handle->buf_count; i++) {
        if (handle->buf_ptrs[i]) {
            munmap(handle->buf_ptrs[i], handle->buf_infos[i].length);
            handle->buf_ptrs[i] = NULL;
        }
    }

    if (handle->fd >= 0) {
        close(handle->fd);
        handle->fd = -1;
    }
    pthread_mutex_destroy(&handle->lock);
}

int v4l2_set_exposure(v4l2_handle_t *handle, int value)
{
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
    ctrl.value = value;
    return ioctl(handle->fd, VIDIOC_S_CTRL, &ctrl);
}

int v4l2_query_controls(v4l2_handle_t *handle)
{
    (void)handle;
    return 0;
}
