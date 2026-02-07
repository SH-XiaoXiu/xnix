# 服务元数据
set(SVC_ISO TRUE)           # 打包到 ISO
set(SVC_INSTALL_DIR "sbin") # 核心服务，安装到 sbin

add_executable(ramfsd.elf
        $<TARGET_OBJECTS:crt0>
        main.c ramfs.c 
)

target_compile_options(ramfsd.elf PRIVATE ${USER_C_FLAGS})

target_include_directories(ramfsd.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/user/libs/libc/include
        ${CMAKE_SOURCE_DIR}/user/libs/libd/include
        ${CMAKE_SOURCE_DIR}/user/libs/libvfs/include
        ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(ramfsd.elf PRIVATE c d vfs pthread)

target_link_options(ramfsd.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
)

add_custom_command(TARGET ramfsd.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ramfsd.elf> ${CMAKE_BINARY_DIR}/ramfsd.elf
)
