# serial - 串口驱动

set(APP_NAME "serial")
set(APP_SOURCES main.c serial.c)
set(APP_LIBS c driver pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
