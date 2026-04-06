# libproc - 进程构建/系统管理辅助
#
# 当前仍承载 proc builder 与 image builder。
# 长期会把普通用户态可见的高层进程语义逐步迁到 libposix，
# libproc 自身收缩为系统服务/引导/管理工具辅助库。
set(LIB_NAME "proc")
set(LIB_DEPS "c")
