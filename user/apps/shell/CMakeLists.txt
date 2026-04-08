# shell - 交互式 Shell
#
# 当前仍直接依赖 proc builder：
# - 需要显式构造 stdio handle
# - 需要构造管道两端 handle
# 这部分后续在 libposix 有足够高层接口后再迁移

set(APP_NAME "shell")
set(APP_SOURCES main.c path.c line.c)
set(APP_LIBS c sys pthread proc posix)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
