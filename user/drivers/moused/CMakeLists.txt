# moused - PS/2 鼠标驱动

set(APP_NAME "moused")
set(APP_SOURCES main.c ps2.c)
set(APP_LIBS c d pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
