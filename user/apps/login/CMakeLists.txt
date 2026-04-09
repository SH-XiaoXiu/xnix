# login - 用户登录程序

set(APP_NAME "login")
set(APP_SOURCES main.c)
set(APP_LIBS c sys proc posix)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
