cmake_minimum_required(VERSION 3.10)
project(imx6ull_dvr C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(imx6ull_dvr
    app/main.c
    app/dvr_daemon.c
    core/config_manager.c
    core/video_pipeline.c
    core/encoder.c
    core/recorder.c
    drivers/camera/v4l2_capture.c
    drivers/storage/sdcard_manager.c
    drivers/sensors/gsensor.c
    utils/frame_queue.c
)

target_include_directories(imx6ull_dvr PRIVATE
    include
    app
    core
    drivers/camera
    drivers/storage
    drivers/sensors
    utils
)

target_compile_definitions(imx6ull_dvr PRIVATE _GNU_SOURCE _REENTRANT)
target_compile_options(imx6ull_dvr PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_link_libraries(imx6ull_dvr PRIVATE pthread m x264)
