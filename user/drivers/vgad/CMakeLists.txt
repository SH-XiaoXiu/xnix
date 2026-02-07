# vgad - VGA 驱动

set(APP_NAME "vgad")
set(APP_SOURCES main.c vga.c)
set(APP_LIBS c d pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
