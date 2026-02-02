# rootfsd 驱动配置
set(DRIVER_EXTRA_SOURCES
        ${CMAKE_SOURCE_DIR}/user/drivers/fatfsd/fatfs_vfs.c
)
set(DRIVER_EXTRA_INCLUDES
        ${CMAKE_SOURCE_DIR}/user/drivers/fatfsd
)
set(DRIVER_EXTRA_LIBS fatfs)
