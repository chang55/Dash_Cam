# 行车记录仪嵌入式项目说明

## 1. 项目定位

本项目是一个面向 Linux 嵌入式平台的行车记录仪软件原型，目标平台按 i.MX6ULL / ARM Cortex-A7 规划。当前重点是打通最小录像链路：V4L2 摄像头采集、YUV422 到 I420 转换、x264 H.264 编码、H.264 裸流分段写入、循环清理和紧急录像锁定。

项目仍处于开发阶段，不是完整产品固件。LCD 预览、真实 G-sensor 驱动、GPIO 按键中断、电源管理、MP4/MKV 封装、GPS 叠加和长时间稳定性测试还需要继续完善。

## 2. 当前项目边界

当前已经纳入代码边界的内容：

- DVR 守护进程入口、初始化、主循环和退出清理。
- 默认配置和简单 `key=value` 配置文件加载。
- 基础日志宏、错误码和公共数据结构。
- V4L2 摄像头采集封装，默认设备为 `/dev/video0`。
- 线程安全帧队列，连接采集线程和编码线程。
- x264 软件编码器封装。
- UYVY/YUYV 到 I420 的基础像素格式转换。
- H.264 裸流分段录像。
- 普通录像循环清理和紧急录像文件保护。
- 紧急录像预录缓存和后录恢复逻辑。
- SD 卡存储状态查询基础接口。
- G-sensor 接口占位，后续可替换为真实驱动读取。
- Makefile 和 CMake 构建入口。

当前仍不属于已完成边界的内容：

- 音频采集、音频编码和音视频同步。
- MP4/MKV 等标准容器封装。
- LCD 预览显示和菜单 UI。
- 真实 GPIO 按键中断接入。
- 真实 G-sensor、GPS、电源、温度等外设驱动。
- SD 卡热插拔事件的完整异步处理。
- 硬件编码器适配。
- 产品级异常恢复、掉电保护和压力测试。

## 3. 目录结构

```text
.
├── app/                 # 程序入口和 DVR 守护进程
├── core/                # 视频流水线、编码器、录像器
├── drivers/
│   ├── camera/          # V4L2 摄像头采集
│   ├── sensors/         # G-sensor 接口占位
│   └── storage/         # SD 卡/存储状态查询
├── include/             # 公共类型、日志、配置、错误码
├── utils/               # 通用工具，当前包含帧队列
├── config/              # 默认配置文件
├── middleware/          # 预留目录
├── scripts/             # 预留目录
├── docs/                # 预留目录
├── Makefile             # Make 构建入口
└── CMakeLists.txt       # CMake 构建入口
```

## 4. 核心运行链路

```text
系统上电
  -> app/main.c
  -> dvr_daemon_init()
  -> 加载默认配置和 /etc/dvr.conf
  -> 初始化 V4L2 摄像头
  -> 初始化 x264 编码器
  -> 初始化录像器和帧队列
  -> 自动开始录像
  -> 采集线程读取 V4L2 帧
  -> 帧数据复制进入线程安全队列
  -> 编码线程取帧并转换为 I420
  -> x264 编码为 H.264 Annex-B
  -> recorder 写入 .h264 分段文件
  -> 空间不足时删除最旧普通录像
  -> 紧急事件触发 EMERGENCY 文件并写入预录缓存
  -> 退出信号触发线程、摄像头、编码器、录像文件清理
```

## 5. 构建说明

默认交叉编译工具链前缀：

```bash
arm-linux-gnueabihf-
```

构建命令：

```bash
make
```

如果在本机 Linux 环境做语法验证，可以清空交叉编译前缀和目标 CPU 参数：

```bash
make CROSS_COMPILE= TARGET_CFLAGS=
```

当前链接依赖：

- pthread
- math
- x264

目标产物：

```text
imx6ull_dvr
```

## 6. 配置文件

默认配置样例位于：

```text
config/dvr.conf
```

程序启动时优先尝试读取：

```text
/etc/dvr.conf
```

读取失败时使用内置默认值。当前支持的配置项包括：

- `width`
- `height`
- `fps`
- `bitrate_kbps`
- `gop_size`
- `segment_minutes`
- `emergency_pre_sec`
- `emergency_post_sec`
- `record_audio`
- `gps_overlay`
- `auto_start`
- `storage_path`

## 7. 后续完善状态

已完成：

1. 统一 `recorder`、`video_pipeline` 等文件命名和头文件引用。
2. 补齐基础日志 `logger.h`。
3. 调整 Makefile，只保留真实存在并已接入的源码文件。
4. 实现线程安全帧队列，打通采集线程到编码线程的数据流。
5. 实现 UYVY/YUYV 到 I420 的基础转换。
6. 完成 `video_pipeline_deinit()` 和资源释放。
7. 跑通 H.264 裸流分段录像的代码链路。
8. 补齐配置文件加载和默认配置回退。
9. 增加 SD 卡/存储状态查询基础接口。
10. 实现紧急录像预录缓存、后录时长和紧急文件保护策略。
11. 增加 G-sensor 接口占位，守护进程已预留碰撞事件接入口。

待完成：

12. 根据实际平台性能评估 x264 是否足够，必要时切换硬件编码。
13. 增加 MP4/MKV 封装、GPS 信息写入和时间戳管理。
14. 增加长时间录像压力测试、掉电恢复测试和 SD 卡异常测试。

## 8. 已知限制

- 当前录像输出为 `.h264` 裸流，不是 MP4 文件。
- G-sensor 驱动当前返回空数据，需要替换为真实传感器读取。
- GPIO 按键仍是平台相关占位逻辑。
- SD 卡热插拔还没有通过 udev/netlink 或 GPIO 检测做完整事件化处理。
- 当前开发机缺少 `make`、`gcc/clang`、`cmake`，本次修改未在当前机器完成真实编译验证。

