# wsd - Window Server Daemon

set(APP_NAME "wsd")
set(APP_SOURCES main.c window.c compositor.c cursor.c input.c)
set(APP_LIBS c d pthread font)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
