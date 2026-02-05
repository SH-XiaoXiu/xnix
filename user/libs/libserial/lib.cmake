# libserial 配置
set(LIB_NAME "serial")
set(LIB_DEPS "ipc")       # 依赖 libipc(libipc 已经依赖 libc)
set(LIB_KERNEL OFF)       # 使用 SDK 头文件，不需要内核头文件
