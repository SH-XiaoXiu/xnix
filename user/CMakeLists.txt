# 用户态程序构建

set(USER_C_FLAGS
    -m32 -march=i386
    -ffreestanding -nostdlib -fno-builtin -fno-stack-protector
    -Wall -Wextra
    -g
)

add_executable(init.elf
    crt0.s
    init.c
)

target_compile_options(init.elf PRIVATE ${USER_C_FLAGS})
target_include_directories(init.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/main/include
        ${ARCH_DIR}/include
        ${CMAKE_BINARY_DIR}/include
)
target_link_options(init.elf PRIVATE
    -m32 -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user.ld -nostdlib
)

# 将 init.elf 复制到构建目录根部方便引用
add_custom_command(TARGET init.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:init.elf> ${CMAKE_BINARY_DIR}/init.elf
)

add_executable(seriald.elf
        crt0.s
        seriald.c
)

target_compile_options(seriald.elf PRIVATE ${USER_C_FLAGS})
target_include_directories(seriald.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/main/include
        ${ARCH_DIR}/include
        ${CMAKE_BINARY_DIR}/include
)
target_link_options(seriald.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user.ld -nostdlib
)

add_custom_command(TARGET seriald.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:seriald.elf> ${CMAKE_BINARY_DIR}/seriald.elf
)
