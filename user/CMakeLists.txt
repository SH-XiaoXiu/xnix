# 用户态程序构建

set(USER_C_FLAGS
    -m32 -march=i386
    -ffreestanding -nostdlib -fno-builtin -fno-stack-protector
    -Wall -Wextra
    -g
)

# ============================================
# libc - 用户态标准库
# ============================================
file(GLOB_RECURSE LIBC_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/src/*.c
)

add_library(c STATIC
        ${LIBC_SOURCES}
)

target_compile_options(c PRIVATE ${USER_C_FLAGS})
target_include_directories(c PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
        ${CMAKE_SOURCE_DIR}/main/include  # for xnix/udm/*.h
)

# ============================================
# libpthread - 用户态多线程库
# ============================================
add_subdirectory(libpthread)

# ============================================
# libudm - UDM 框架库
# ============================================
file(GLOB_RECURSE LIBUDM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/libudm/src/*.c
)

add_library(udm STATIC
        ${LIBUDM_SOURCES}
)

target_compile_options(udm PRIVATE ${USER_C_FLAGS})
target_include_directories(udm PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libudm/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
        ${CMAKE_SOURCE_DIR}/main/include
)
target_link_libraries(udm PUBLIC c)

# ============================================
# init.elf
# ============================================
add_executable(init.elf
        libc/src/crt0.s
    init.c
)

target_compile_options(init.elf PRIVATE ${USER_C_FLAGS})
target_include_directories(init.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(init.elf PRIVATE c)
target_link_options(init.elf PRIVATE
    -m32 -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user.ld -nostdlib
)

add_custom_command(TARGET init.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:init.elf> ${CMAKE_BINARY_DIR}/init.elf
)

# ============================================
# seriald.elf - UDM 串口驱动
# ============================================
add_executable(seriald.elf
        libc/src/crt0.s
        drivers/seriald/main.c
        drivers/seriald/serial.c
)

target_compile_options(seriald.elf PRIVATE ${USER_C_FLAGS})
target_include_directories(seriald.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libudm/include
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(seriald.elf PRIVATE c udm)
target_link_options(seriald.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user.ld -nostdlib
)

add_custom_command(TARGET seriald.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:seriald.elf> ${CMAKE_BINARY_DIR}/seriald.elf
)

# ============================================
# kbd.elf - UDM 键盘驱动
# ============================================
add_executable(kbd.elf
        libc/src/crt0.s
        drivers/kbd/main.c
        drivers/kbd/scancode.c
)

target_compile_options(kbd.elf PRIVATE ${USER_C_FLAGS})
target_include_directories(kbd.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libudm/include
        ${CMAKE_CURRENT_SOURCE_DIR}/drivers/kbd
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(kbd.elf PRIVATE c udm)
target_link_options(kbd.elf PRIVATE
        -m32 -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/user.ld -nostdlib
)

add_custom_command(TARGET kbd.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kbd.elf> ${CMAKE_BINARY_DIR}/kbd.elf
)

# ============================================
# Demo 程序 (自动发现)
# ============================================
# 确保 include 目录存在
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include)

add_subdirectory(demo)

# 传递 demo 变量给父级
set(DEMO_TARGETS "${DEMO_TARGETS}" PARENT_SCOPE)
set(DEMO_NAMES "${DEMO_NAMES}" PARENT_SCOPE)
