# termd - 终端服务
set(APP_NAME "termd")
set(APP_SOURCES main.c)
set(APP_LIBS c pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
