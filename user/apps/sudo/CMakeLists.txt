# sudo - 权限提升客户端 (通过 userd 认证并执行)

set(APP_NAME "sudo")
set(APP_SOURCES main.c)
set(APP_LIBS c sys posix)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
