# vfs - VFS 服务

set(APP_NAME "vfs")
set(APP_SOURCES main.c)
set(APP_LIBS c sys pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
