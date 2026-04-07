# display - unified text display driver

set(APP_NAME "display")
set(APP_SOURCES main.c vga.c)
set(APP_LIBS c driver pthread font)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
