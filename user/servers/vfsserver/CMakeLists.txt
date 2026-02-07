# vfsserver - VFS 服务

set(APP_NAME "vfsserver")
set(APP_SOURCES main.c)
set(APP_LIBS c d vfs pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
