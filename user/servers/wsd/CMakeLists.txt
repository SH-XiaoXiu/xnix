# ws - Window Server

set(APP_NAME "ws")
set(APP_SOURCES main.c window.c compositor.c cursor.c input.c)
set(APP_LIBS c sys pthread font)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
