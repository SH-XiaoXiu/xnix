# fbcond - Framebuffer Console

set(APP_NAME "fbcond")
set(APP_SOURCES main.c)
set(APP_LIBS c d pthread font)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
