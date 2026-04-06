# devfs - 设备文件系统服务
#
# 提供设备文件系统，挂载到 /dev

set(APP_NAME "devfs")
set(APP_SOURCES main.c)
set(APP_LIBS c d vfs pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
