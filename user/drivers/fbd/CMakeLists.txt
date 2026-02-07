# fbd - Framebuffer 驱动

set(APP_NAME "fbd")
set(APP_SOURCES main.c)
set(APP_LIBS c d vfs pthread)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
