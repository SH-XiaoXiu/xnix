cmake_minimum_required(VERSION 3.20)
project(Xnix C ASM)
set(CMAKE_C_STANDARD 11)

# ============================================
# 架构设置
# ============================================
set(ARCH "x86")
set(ARCH_DIR "${CMAKE_SOURCE_DIR}/main/arch/${ARCH}")

# ============================================
# 编译选项
# ============================================
set(CMAKE_C_FLAGS "-m32 -ffreestanding -fno-stack-protector -fno-pie -nostdlib -nostartfiles -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_ASM_FLAGS "-m32 -g")
set(CMAKE_EXE_LINKER_FLAGS "-m32 -T${ARCH_DIR}/boot/kernel.ld -nostdlib -no-pie -Wl,--build-id=none")

# 内核配置
set(CFG_SCHED_HZ 100)
set(CFG_THREAD_STACK_SIZE 4096)
set(CFG_DEF_TIME_SLICE 10)
set(CFG_INITIAL_PROCESSES 64)
set(CFG_INITIAL_THREADS 256)
set(CFG_MAX_CONSOLES 4)

# CPU 配置
set(CFG_MAX_CPUS 1)
option(ENABLE_SMP "Enable SMP support" ON)
if(ENABLE_SMP)
    set(CFG_MAX_CPUS 8)
    add_definitions(-DENABLE_SMP)
endif()

# VMM 调试
option(ENABLE_VMM_DEBUG "Enable VMM debug logging" OFF)
if(ENABLE_VMM_DEBUG)
    add_definitions(-DENABLE_VMM_DEBUG)
endif()

# 内核直接映射大小（高半核架构）
# 物理内存 [0, CFG_KERNEL_DIRECT_MAP_MB] 映射到虚拟地址 [0xC0000000, ...]
# 建议值：嵌入式 16-32MB，通用 64MB，大内存系统 128-256MB
set(CFG_KERNEL_DIRECT_MAP_MB 64)

# IPC 配置
set(CFG_CAP_TABLE_SIZE 256)      # 每个进程最多句柄数
set(CFG_IPC_MSG_REGS 8)          # 短消息寄存器数量
set(CFG_IPC_MSG_CAPS_MAX 4)      # 消息中最多能力句柄数
option(CFG_IPC_MSG_POOL "Enable IPC global message pool for async buffering" ON)

# IRQ 配置
set(CFG_IRQ_USER_BUF_SIZE 64)    # IRQ 用户态缓冲区大小

option(CFG_DEBUG_MM "Debug memory manager" OFF)
option(CFG_DEBUG_SCHED "Debug scheduler" OFF)
option(CFG_DEBUG_IPC "Debug IPC" OFF)
option(CFG_DEBUG "Enable kernel debug logging" ON)

configure_file(
        ${CMAKE_SOURCE_DIR}/main/include/xnix/config.h.in
        ${CMAKE_BINARY_DIR}/include/xnix/config.h
)

# ============================================
# 源文件收集
# ============================================
file(GLOB_RECURSE KERNEL_C "${CMAKE_SOURCE_DIR}/main/kernel/*.c")
file(GLOB_RECURSE LIB_C "${CMAKE_SOURCE_DIR}/main/lib/*.c")
file(GLOB_RECURSE DRIVERS_C "${CMAKE_SOURCE_DIR}/main/drivers/*.c")
file(GLOB_RECURSE ARCH_C "${ARCH_DIR}/*.c")
file(GLOB_RECURSE ARCH_ASM_LOWER "${ARCH_DIR}/*.s")
file(GLOB_RECURSE ARCH_ASM_UPPER "${ARCH_DIR}/*.S")
set(KERNEL_SOURCES ${KERNEL_C} ${LIB_C} ${DRIVERS_C} ${ARCH_C} ${ARCH_ASM_LOWER} ${ARCH_ASM_UPPER})

# ============================================
# 创建 ELF 内核目标
# ============================================
add_executable(xnix.elf ${KERNEL_SOURCES})

# ============================================
# 头文件搜索路径
# ============================================
target_include_directories(xnix.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/main/include    # <xnix/xxx>, <arch/xxx>, <drivers/xxx>
        ${CMAKE_SOURCE_DIR}/main/include/arch # <hal/xxx>
        ${CMAKE_SOURCE_DIR}/main            # <kernel/xxx>
        ${CMAKE_SOURCE_DIR}/main/lib        # <sync/xxx>
        ${CMAKE_BINARY_DIR}/include
        ${ARCH_DIR}/include
)

# 为 CLion 索引添加头文件
file(GLOB_RECURSE ALL_HEADERS "${CMAKE_SOURCE_DIR}/main/*.h")
target_sources(xnix.elf PRIVATE ${ALL_HEADERS})

# ============================================
# 子目录
# ============================================
add_subdirectory(user) # 添加用户态构建

# ============================================
# ISO 生成
# ============================================
set(ISO_DIR ${CMAKE_BINARY_DIR}/iso)

# 生成 grub.cfg (在 configure 时)
set(GRUB_CFG_CONTENT "set timeout=0\nset default=0\nmenuentry \"Xnix\" {\n")
string(APPEND GRUB_CFG_CONTENT "  multiboot /boot/xnix.elf xnix.initmod=0 xnix.serialmod=1\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/init.elf\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/seriald.elf\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/kbd.elf\n")
string(APPEND GRUB_CFG_CONTENT "  module /boot/shell.elf\n")
foreach(DEMO_ELF ${DEMO_TARGETS})
    string(APPEND GRUB_CFG_CONTENT "  module /boot/${DEMO_ELF}\n")
endforeach()
string(APPEND GRUB_CFG_CONTENT "}\n")
file(MAKE_DIRECTORY ${ISO_DIR}/boot/grub)
file(WRITE ${ISO_DIR}/boot/grub/grub.cfg "${GRUB_CFG_CONTENT}")

# 构建 demo 依赖和复制命令列表
set(ISO_DEPENDS xnix.elf init.elf seriald.elf kbd.elf shell.elf)
set(ISO_COPY_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:xnix.elf> ${ISO_DIR}/boot/xnix.elf
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/init.elf ${ISO_DIR}/boot/init.elf
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/seriald.elf ${ISO_DIR}/boot/seriald.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/kbd.elf ${ISO_DIR}/boot/kbd.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/shell.elf ${ISO_DIR}/boot/shell.elf
)

foreach(DEMO_ELF ${DEMO_TARGETS})
    list(APPEND ISO_DEPENDS ${DEMO_ELF})
    list(APPEND ISO_COPY_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${DEMO_ELF} ${ISO_DIR}/boot/${DEMO_ELF}
    )
endforeach()

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/xnix.iso
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_DIR}/boot/grub
    ${ISO_COPY_COMMANDS}
    COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/xnix.iso ${ISO_DIR} 2>/dev/null || true
    DEPENDS ${ISO_DEPENDS}
    COMMENT "创建可引导 ISO (demos: ${DEMO_TARGETS})..."
)
add_custom_target(iso DEPENDS ${CMAKE_BINARY_DIR}/xnix.iso)

# ============================================
# QEMU 运行/调试
# ============================================
add_custom_target(run
        COMMAND qemu-system-i386 -cdrom ${CMAKE_BINARY_DIR}/xnix.iso -serial stdio -no-reboot -smp ${CFG_MAX_CPUS}
        DEPENDS iso
        COMMENT "在 QEMU 中运行 Xnix..."
        VERBATIM
)

add_custom_target(debug
        COMMAND qemu-system-i386 -kernel ${CMAKE_BINARY_DIR}/xnix.elf -s -S
        DEPENDS xnix.elf
        COMMENT "在 QEMU 调试模式下运行 Xnix (等待 GDB 连接端口1234)..."
        VERBATIM
)
