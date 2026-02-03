# rootfsd 驱动配置
set(SERVER_EXTRA_SOURCES
        ${CMAKE_SOURCE_DIR}/user/servers/FatfsServer/fatfs_vfs.c
)
set(SERVER_EXTRA_INCLUDES
        ${CMAKE_SOURCE_DIR}/user/servers/FatfsServer
)
set(SERVER_EXTRA_LIBS fatfs)
