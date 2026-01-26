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
set(CMAKE_EXE_LINKER_FLAGS "-m32 -T${ARCH_DIR}/boot/kernel.ld -nostdlib -no-pie")

# 内核配置
set(CFG_SCHED_HZ 100)
set(CFG_THREAD_STACK_SIZE 4096)
set(CFG_DEF_TIME_SLICE 10)
set(CFG_INITIAL_PROCESSES 64)
set(CFG_INITIAL_THREADS 256)
set(CFG_MAX_CONSOLES 4)

# CPU 配置
set(CFG_MAX_CPUS 1)
option(ENABLE_SMP "Enable SMP support" OFF)
if(ENABLE_SMP)
    set(CFG_MAX_CPUS 8)
    add_definitions(-DENABLE_SMP)
endif()

# VMM 调试
option(ENABLE_VMM_DEBUG "Enable VMM debug logging" OFF)
if(ENABLE_VMM_DEBUG)
    add_definitions(-DENABLE_VMM_DEBUG)
endif()

# 低端恒等映射窗口(用于内核直接访问物理地址)
# x86 non-PAE 下，为避免与用户态地址布局冲突，应保持较小(例如 64MB/128MB)。
set(CFG_KERNEL_IDMAP_MB 64)

# IPC 配置
set(CFG_CAP_TABLE_SIZE 256)      # 每个进程最多句柄数
set(CFG_IPC_MSG_REGS 8)          # 短消息寄存器数量
set(CFG_IPC_MSG_CAPS_MAX 4)      # 消息中最多能力句柄数
option(CFG_IPC_MSG_POOL "Enable IPC global message pool for async buffering" ON)

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
file(GLOB_RECURSE ARCH_ASM "${ARCH_DIR}/*.s")
set(KERNEL_SOURCES ${KERNEL_C} ${LIB_C} ${DRIVERS_C} ${ARCH_C} ${ARCH_ASM})

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

add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/xnix.iso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_DIR}/boot/grub
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:xnix.elf> ${ISO_DIR}/boot/xnix.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/init.elf ${ISO_DIR}/boot/init.elf
        COMMAND ${CMAKE_COMMAND} -E echo "set timeout=0" > ${ISO_DIR}/boot/grub/grub.cfg
        COMMAND ${CMAKE_COMMAND} -E echo "set default=0" >> ${ISO_DIR}/boot/grub/grub.cfg
        COMMAND ${CMAKE_COMMAND} -E echo "menuentry \"Xnix\" { " >> ${ISO_DIR}/boot/grub/grub.cfg
        COMMAND ${CMAKE_COMMAND} -E echo "  multiboot /boot/xnix.elf" >> ${ISO_DIR}/boot/grub/grub.cfg
        COMMAND ${CMAKE_COMMAND} -E echo "  module /boot/init.elf" >> ${ISO_DIR}/boot/grub/grub.cfg
        COMMAND ${CMAKE_COMMAND} -E echo "}" >> ${ISO_DIR}/boot/grub/grub.cfg
        COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/xnix.iso ${ISO_DIR} 2>/dev/null || true
        DEPENDS xnix.elf init.elf
        COMMENT "创建可引导 ISO..."
)
add_custom_target(iso DEPENDS ${CMAKE_BINARY_DIR}/xnix.iso)

# ============================================
# QEMU 运行/调试
# ============================================
add_custom_target(run
        COMMAND qemu-system-i386 -cdrom ${CMAKE_BINARY_DIR}/xnix.iso
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
