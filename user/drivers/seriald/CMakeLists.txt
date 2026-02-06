# 使用 libipc 封装，提供 serial_init/write/putchar/set_color API
add_executable(seriald.elf
        $<TARGET_OBJECTS:crt0>
        main.c
        serial.c
)

target_compile_options(seriald.elf PRIVATE ${USER_C_FLAGS})

target_include_directories(seriald.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/user/libs/libc/include
        ${CMAKE_SOURCE_DIR}/user/libs/libd/include
        ${CMAKE_SOURCE_DIR}/user/libs/libvfs/include
        ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(seriald.elf PRIVATE c d vfs pthread)

target_link_options(seriald.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
)

add_custom_command(TARGET seriald.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:seriald.elf> ${CMAKE_BINARY_DIR}/seriald.elf
)
