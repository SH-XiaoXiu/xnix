# 服务元数据
set(SVC_ISO TRUE)           # 打包到 ISO
set(SVC_INSTALL_DIR "sbin") # 核心服务，安装到 sbin

add_executable(fatfsd.elf
        $<TARGET_OBJECTS:crt0>
        ata.c
        diskio.c
        fatfs_vfs.c
        main.c
)

target_compile_options(fatfsd.elf PRIVATE ${USER_C_FLAGS})

target_include_directories(fatfsd.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/user/libs/libc/include
        ${CMAKE_SOURCE_DIR}/user/libs/libd/include
        ${CMAKE_SOURCE_DIR}/user/libs/libvfs/include
        ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
        ${CMAKE_SOURCE_DIR}/user/tplibs/ff16  # fatfs 头文件
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(fatfsd.elf PRIVATE c d vfs pthread fatfs)

target_link_options(fatfsd.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
)

add_custom_command(TARGET fatfsd.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fatfsd.elf> ${CMAKE_BINARY_DIR}/fatfsd.elf
)
