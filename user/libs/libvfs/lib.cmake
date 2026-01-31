# libvfs 配置 - VFS 服务框架
set(LIB_NAME "vfs")
set(LIB_DEPS "c;udm")     # 显式依赖 libc 确保 include 顺序正确
set(LIB_KERNEL ON)        # 需要内核头文件 (<xnix/udm/vfs.h>)
