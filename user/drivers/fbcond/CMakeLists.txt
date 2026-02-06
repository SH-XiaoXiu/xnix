add_executable(fbcond.elf
        $<TARGET_OBJECTS:crt0>
        main.c
)

target_compile_options(fbcond.elf PRIVATE ${USER_C_FLAGS})

target_include_directories(fbcond.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/user/libs/libc/include
        ${CMAKE_SOURCE_DIR}/user/libs/libd/include
        ${CMAKE_SOURCE_DIR}/user/libs/libpthread/include
        ${CMAKE_BINARY_DIR}/include
)

target_link_libraries(fbcond.elf PRIVATE c d pthread font)

target_link_options(fbcond.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_SOURCE_DIR}/user/libs/user.ld -nostdlib
)

add_custom_command(TARGET fbcond.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fbcond.elf> ${CMAKE_BINARY_DIR}/fbcond.elf
)

