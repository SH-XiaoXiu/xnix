# userd - 用户管理服务

set(APP_NAME "userd")
set(APP_SOURCES main.c passwd.c session.c)
set(APP_LIBS c sys proc posix)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
