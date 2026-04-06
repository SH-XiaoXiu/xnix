# sudo - 权限提升客户端
#
# 当前仍直接依赖 proc builder：
# - 需要向 sudod 发送完整 exec_args
# 后续如抽出独立的 posix/privileged spawn 请求结构，再从 proc 迁走

set(APP_NAME "sudo")
set(APP_SOURCES main.c)
set(APP_LIBS c sys pthread proc posix)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
