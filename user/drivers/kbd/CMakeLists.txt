# kbd - 键盘驱动

set(APP_NAME "kbd")
set(APP_SOURCES main.c scancode.c)
set(APP_LIBS c d pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
