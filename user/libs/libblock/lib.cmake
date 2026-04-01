# libblock - 块设备抽象层
#
# 提供统一的块设备接口，支持 ATA、SCSI、VirtIO 等不同类型磁盘

set(LIB_NAME "block")
set(LIB_DEPS "c")
set(LIB_KERNEL ON)
