# sudo - 权限提升客户端

set(APP_NAME "sudo")
set(APP_SOURCES main.c)
set(APP_LIBS c d vfs pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
