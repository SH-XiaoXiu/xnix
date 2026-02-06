cmake_minimum_required(VERSION 3.20)
project(Xnix C ASM)
set(CMAKE_C_STANDARD 11)

# ============================================
# 项目结构
# ============================================
# main/   - 内核（本文件负责编译）
# user/   - 用户态程序
# iso/    - ISO 打包模块（可选）

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

# ============================================
# 编译裁切：子系统开关
# ============================================
option(CFG_ENABLE_IPC "Enable IPC subsystem" ON)
option(CFG_ENABLE_VFS "Enable VFS subsystem" ON)
option(CFG_ENABLE_INPUT "Enable Input subsystem" ON)

# 依赖检查
if (CFG_ENABLE_VFS AND NOT CFG_ENABLE_IPC)
    message(WARNING "VFS requires IPC, enabling IPC automatically")
    set(CFG_ENABLE_IPC ON CACHE BOOL "" FORCE)
endif ()
if (CFG_ENABLE_INPUT AND NOT CFG_ENABLE_IPC)
    message(WARNING "Input requires IPC, enabling IPC automatically")
    set(CFG_ENABLE_IPC ON CACHE BOOL "" FORCE)
endif ()

# ============================================
# 类型大小配置
# ============================================
set(CFG_TID_BITS 32 CACHE STRING "TID type bits (16 or 32)")
set(CFG_PRIORITY_BITS 32 CACHE STRING "Priority type bits (8 or 32)")
set(CFG_SLICE_BITS 32 CACHE STRING "Time slice type bits (16 or 32)")

# ============================================
# 资源分配模式
# ============================================
option(CFG_TID_DYNAMIC "Enable dynamic TID allocation" ON)
set(CFG_MAX_THREADS 4096 CACHE STRING "Thread table size")

option(CFG_PID_DYNAMIC "Enable dynamic PID allocation" ON)
set(CFG_MAX_PROCESSES 1024 CACHE STRING "Process table size")

option(CFG_BITMAP_DYNAMIC "Enable dynamic bitmap allocator" ON)

# ============================================
# 调度器配置
# ============================================
set(CFG_SCHED_HZ 100 CACHE STRING "Scheduler tick frequency (Hz)")
set(CFG_DEF_TIME_SLICE 10 CACHE STRING "Default thread time slice (ticks)")

# ============================================
# 内存配置
# ============================================
set(CFG_THREAD_STACK_SIZE 4096 CACHE STRING "Thread kernel stack size (bytes)")
set(CFG_KERNEL_DIRECT_MAP_MB 64 CACHE STRING "Kernel direct mapping size (MB)")

# ============================================
# CPU 配置
# ============================================
set(CFG_MAX_CPUS 1)
option(ENABLE_SMP "Enable SMP support" ON)
if (ENABLE_SMP)
    set(CFG_MAX_CPUS 8)
    add_definitions(-DENABLE_SMP)
endif ()

# ============================================
# IPC 配置
# ============================================
set(CFG_IPC_MSG_REGS 8 CACHE STRING "Number of short message registers")
set(CFG_IPC_MSG_HANDLES_MAX 4 CACHE STRING "Max handles per message")
set(CFG_HANDLE_TABLE_SIZE 1024 CACHE STRING "Handle table initial size")
option(CFG_IPC_MSG_POOL "Enable IPC global message pool for async buffering" ON)

# ============================================
# IRQ 配置
# ============================================
set(CFG_IRQ_USER_BUF_SIZE 64 CACHE STRING "IRQ user-mode buffer size")

# ============================================
# 调试配置
# ============================================
option(CFG_DEBUG "Enable kernel debug logging" OFF)
option(CONFIG_DEBUG_CONSOLE "Enable SYS_DEBUG_PUT syscall for early debugging" ON)

# ============================================
# 打包配置
# ============================================
option(PACK_BIN_TO_ROOTFS "Pack all bin tools to rootfs.img by default" OFF)

configure_file(
        ${CMAKE_SOURCE_DIR}/main/include/xnix/config.h.in
        ${CMAKE_BINARY_DIR}/include/xnix/config.h
)

# ============================================
# 内核源文件（分组为 4 个 object library）
# ============================================

# ─── 共享 include 路径 ───
set(SHARED_INCLUDES
        ${CMAKE_SOURCE_DIR}/sdk/include      # <xnix/abi/xxx.h>
        ${CMAKE_SOURCE_DIR}/main/include     # <xnix/xxx.h> <arch/xxx.h> <drivers/xxx.h>
        ${ARCH_DIR}/include                  # <asm/xxx.h>
        ${CMAKE_BINARY_DIR}/include          # <xnix/config.h>
)

# ─── 1. Kernel Core 源文件 ───
set(KERNEL_REQUIRED_DIRS boot mm sched irq sys process hal exec udm log)
set(KERNEL_CORE_SOURCES "")

list(APPEND KERNEL_CORE_SOURCES "${CMAKE_SOURCE_DIR}/main/kernel/xnix.c")

foreach (SUBDIR ${KERNEL_REQUIRED_DIRS})
    file(GLOB_RECURSE SUBDIR_C "${CMAKE_SOURCE_DIR}/main/kernel/${SUBDIR}/*.c")
    list(APPEND KERNEL_CORE_SOURCES ${SUBDIR_C})
endforeach ()

# 可选子系统
if (CFG_ENABLE_IPC)
    file(GLOB_RECURSE IPC_C "${CMAKE_SOURCE_DIR}/main/kernel/ipc/*.c")
    list(APPEND KERNEL_CORE_SOURCES ${IPC_C})
endif ()

if (CFG_ENABLE_VFS)
    file(GLOB_RECURSE VFS_C "${CMAKE_SOURCE_DIR}/main/kernel/vfs/*.c")
    list(APPEND KERNEL_CORE_SOURCES ${VFS_C})
endif ()

# Permission System
file(GLOB_RECURSE PERM_C "${CMAKE_SOURCE_DIR}/main/kernel/perm/*.c")
list(APPEND KERNEL_CORE_SOURCES ${PERM_C})

# Handle System
file(GLOB_RECURSE HANDLE_C "${CMAKE_SOURCE_DIR}/main/kernel/handle/*.c")
list(APPEND KERNEL_CORE_SOURCES ${HANDLE_C})

if (CFG_ENABLE_INPUT)
    file(GLOB_RECURSE INPUT_C "${CMAKE_SOURCE_DIR}/main/kernel/io/input.c")
    list(APPEND KERNEL_CORE_SOURCES ${INPUT_C})
endif ()

file(GLOB_RECURSE IOPORT_C "${CMAKE_SOURCE_DIR}/main/kernel/io/ioport.c")
list(APPEND KERNEL_CORE_SOURCES ${IOPORT_C})

if (NOT CFG_TID_DYNAMIC)
    list(FILTER KERNEL_CORE_SOURCES EXCLUDE REGEX ".*/tid\\.c$")
endif ()

# ─── 2. Lib 源文件 ───
set(LIB_SOURCES "")
file(GLOB_RECURSE LIB_C "${CMAKE_SOURCE_DIR}/main/lib/*.c")
list(FILTER LIB_C EXCLUDE REGEX ".*_static\\.c$")
list(FILTER LIB_C EXCLUDE REGEX ".*_dynamic\\.c$")

if (CFG_BITMAP_DYNAMIC)
    list(APPEND LIB_C "${CMAKE_SOURCE_DIR}/main/lib/resource_dynamic.c")
else ()
    list(APPEND LIB_C "${CMAKE_SOURCE_DIR}/main/lib/resource_static.c")
endif ()

if (NOT CFG_TID_DYNAMIC)
    list(APPEND LIB_C "${CMAKE_SOURCE_DIR}/main/lib/id_alloc_static.c")
endif ()

set(LIB_SOURCES ${LIB_C})

# ─── 3. Drivers 源文件 ───
file(GLOB_RECURSE DRIVER_SOURCES "${CMAKE_SOURCE_DIR}/main/drivers/*.c")

# ─── 4. Arch 源文件 ───
set(ARCH_SOURCES "")
file(GLOB_RECURSE ARCH_C "${ARCH_DIR}/*.c")
file(GLOB_RECURSE ARCH_ASM_LOWER "${ARCH_DIR}/*.s")
file(GLOB_RECURSE ARCH_ASM_UPPER "${ARCH_DIR}/*.S")
list(APPEND ARCH_SOURCES ${ARCH_C} ${ARCH_ASM_LOWER} ${ARCH_ASM_UPPER})

# ============================================
# 内核目标（拆分为 4 个 object library + 1 个可执行文件）
# ============================================

# ─── arch object library ───
add_library(arch OBJECT ${ARCH_SOURCES})
target_include_directories(arch PRIVATE ${SHARED_INCLUDES})
# arch 不能 include <kernel/...> 或内部头文件

# ─── lib object library ───
add_library(klib OBJECT ${LIB_SOURCES})
target_include_directories(klib PRIVATE ${SHARED_INCLUDES})
# lib 不能 include <kernel/...>

# ─── drivers object library ───
add_library(kdrivers OBJECT ${DRIVER_SOURCES})
target_include_directories(kdrivers PRIVATE
        ${SHARED_INCLUDES}
)
# drivers 需要基础类型定义(asm/types.h),但不依赖其他架构特定功能

# ─── kernel object library ───
add_library(kcore OBJECT ${KERNEL_CORE_SOURCES})
target_include_directories(kcore PRIVATE
        ${SHARED_INCLUDES}
        ${CMAKE_SOURCE_DIR}/main/kernel      # <sched/sched_internal.h> 等内部头文件
)
# 只有 kernel 能访问 main/kernel/ 下的内部头文件

# ─── 链接为可执行文件 ───
add_executable(xnix.elf
        $<TARGET_OBJECTS:arch>
        $<TARGET_OBJECTS:klib>
        $<TARGET_OBJECTS:kdrivers>
        $<TARGET_OBJECTS:kcore>
)

# CLion 索引（为了 IDE 导航方便，包含所有头文件）
file(GLOB_RECURSE ALL_HEADERS "${CMAKE_SOURCE_DIR}/main/*.h")
target_sources(xnix.elf PRIVATE ${ALL_HEADERS})

# 复制内核到 build 根目录
add_custom_command(TARGET xnix.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:xnix.elf> ${CMAKE_BINARY_DIR}/xnix.elf
)

# 导出内核路径
set(KERNEL_ELF "${CMAKE_BINARY_DIR}/xnix.elf" CACHE INTERNAL "Kernel ELF path")

# ============================================
# SDK (Public ABI)
# ============================================
add_subdirectory(sdk)

# ============================================
# 用户态
# ============================================
add_subdirectory(user)

# ============================================
# ISO 打包模块（可选）
# ============================================
option(XNIX_BUILD_ISO "Build ISO image" ON)
if (XNIX_BUILD_ISO AND EXISTS ${CMAKE_SOURCE_DIR}/iso)
    add_subdirectory(iso)
endif ()
