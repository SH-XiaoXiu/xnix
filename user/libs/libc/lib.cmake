# libc 配置
set(LIB_NAME "c")
set(LIB_DEPS "")          # 无依赖
set(LIB_KERNEL ON)        # 需要内核 ABI 头文件（<xnix/abi/*.h>）
set(LIB_EXTRA_INCLUDES "d")  # 需要 d 协议头文件（仅头文件依赖）
