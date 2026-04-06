# libposix - POSIX 风格高层接口骨架
#
# 只承载用户态高层语义，不直接暴露内核私有机制。
# 当前先建立链接/头文件边界，后续逐步承载：
#   - spawn/execve 包装
#   - dup/dup2
#   - pipe
#   - fcntl / poll / select
#   - termios
#   - fork 语义包装（基于底层机制，不等同于 SYS_FORK）

set(LIB_NAME "posix")
set(LIB_DEPS "c;proc")
