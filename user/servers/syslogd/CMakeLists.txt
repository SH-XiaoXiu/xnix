# syslogd - 系统日志服务

set(APP_NAME "syslogd")
set(APP_SOURCES main.c)
set(APP_LIBS c d)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
