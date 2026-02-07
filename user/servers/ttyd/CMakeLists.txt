# 服务元数据
set(SVC_ISO TRUE)           # 打包到 ISO
set(SVC_INSTALL_DIR "sbin") # 核心服务，安装到 sbin

add_executable(ttyd.elf
        $<TARGET_OBJECTS:crt0>
        main.c 
)

target_compile_options(ttyd.elf PRIVATE ${USER_C_FLAGS})

target_include_directories(ttyd.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/user/libs/libc/include
        ${CMAKE_SOURCE_DIR}/user/libs/libd/include
        ${CMAKE_SOURCE_DIR}/user/libs/libvfs/include
        ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(ttyd.elf PRIVATE c d vfs pthread)

target_link_options(ttyd.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
)

add_custom_command(TARGET ttyd.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ttyd.elf> ${CMAKE_BINARY_DIR}/ttyd.elf
)
