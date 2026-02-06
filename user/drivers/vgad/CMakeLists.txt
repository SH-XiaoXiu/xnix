add_executable(vgad.elf
        $<TARGET_OBJECTS:crt0>
        main.c
        vga.c
)

target_compile_options(vgad.elf PRIVATE ${USER_C_FLAGS})

target_include_directories(vgad.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/user/libs/libc/include
        ${CMAKE_SOURCE_DIR}/user/libs/libd/include
        ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(vgad.elf PRIVATE c d pthread)

target_link_options(vgad.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
)

add_custom_command(TARGET vgad.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:vgad.elf> ${CMAKE_BINARY_DIR}/vgad.elf
)
