# fbcond - Framebuffer Console

set(APP_NAME "fbcond")
set(APP_SOURCES main.c)
set(APP_LIBS c driver pthread font)
set(APP_INSTALL_TO_SYSTEM OFF)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
