# sudod - 权限提升守护进程

set(APP_NAME "sudod")
set(APP_SOURCES main.c)
set(APP_LIBS c d vfs pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
