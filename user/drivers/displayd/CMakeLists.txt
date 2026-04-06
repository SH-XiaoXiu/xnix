# displayd - unified text display driver

set(APP_NAME "displayd")
set(APP_SOURCES main.c ../vgad/vga.c)
set(APP_LIBS c driver pthread font)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
