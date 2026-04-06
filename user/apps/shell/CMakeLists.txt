# shell - 交互式 Shell

set(APP_NAME "shell")
set(APP_SOURCES main.c path.c)
set(APP_LIBS c sys pthread proc)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
