# seriald - 串口驱动

set(APP_NAME "seriald")
set(APP_SOURCES main.c serial.c)
set(APP_LIBS c d vfs pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
