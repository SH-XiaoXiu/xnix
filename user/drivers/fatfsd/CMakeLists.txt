# fatfsd - FAT 文件系统驱动

set(APP_NAME "fatfsd")
set(APP_SOURCES ata.c diskio.c fatfs_vfs.c main.c)
set(APP_LIBS c d vfs pthread fatfs)

include(${CMAKE_SOURCE_DIR}/user/app.cmake)
