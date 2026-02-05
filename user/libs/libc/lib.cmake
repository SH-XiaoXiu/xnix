# libc 配置
set(LIB_NAME "c")
set(LIB_DEPS "")          # 无依赖 (serial 是可选的,由程序决定)
set(LIB_KERNEL ON)        # 需要内核 ABI 头文件（<xnix/abi/*.h>）
set(LIB_EXTRA_INCLUDES "d;serial")  # 需要 d 协议头文件和 serial 头文件(仅头文件依赖)
