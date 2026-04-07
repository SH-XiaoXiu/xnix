# ps2 - PS/2 keyboard + mouse unified driver

set(APP_NAME "ps2")
set(APP_SOURCES main.c scancode.c ps2_mouse.c)
set(APP_LIBS c driver pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
