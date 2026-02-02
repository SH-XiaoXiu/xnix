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
option(CFG_ENABLE_CAPS "Enable Capability subsystem" ON)
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

option(CFG_CAP_DYNAMIC "Enable dynamic capability table" ON)
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
set(CFG_CAP_TABLE_SIZE 256 CACHE STRING "Per-process capability table initial size")
set(CFG_IPC_MSG_REGS 8 CACHE STRING "Number of short message registers")
set(CFG_IPC_MSG_CAPS_MAX 4 CACHE STRING "Max capability handles per message")
option(CFG_IPC_MSG_POOL "Enable IPC global message pool for async buffering" ON)

# ============================================
# IRQ 配置
# ============================================
set(CFG_IRQ_USER_BUF_SIZE 64 CACHE STRING "IRQ user-mode buffer size")

# ============================================
# 调试配置
# ============================================
option(CFG_DEBUG "Enable kernel debug logging" OFF)

# ============================================
# 打包配置
# ============================================
option(PACK_BIN_TO_ROOTFS "Pack all bin tools to rootfs.img by default" OFF)

configure_file(
        ${CMAKE_SOURCE_DIR}/main/include/xnix/config.h.in
        ${CMAKE_BINARY_DIR}/include/xnix/config.h
)

# ============================================
# 内核源文件
# ============================================
set(KERNEL_REQUIRED_DIRS boot mm sched irq sys process hal exec udm)
set(KERNEL_SOURCES "")

list(APPEND KERNEL_SOURCES "${CMAKE_SOURCE_DIR}/main/kernel/xnix.c")

foreach (SUBDIR ${KERNEL_REQUIRED_DIRS})
    file(GLOB_RECURSE SUBDIR_C "${CMAKE_SOURCE_DIR}/main/kernel/${SUBDIR}/*.c")
    list(APPEND KERNEL_SOURCES ${SUBDIR_C})
endforeach ()

# 可选子系统
if (CFG_ENABLE_IPC)
    file(GLOB_RECURSE IPC_C "${CMAKE_SOURCE_DIR}/main/kernel/ipc/*.c")
    list(APPEND KERNEL_SOURCES ${IPC_C})
endif ()

if (CFG_ENABLE_VFS)
    file(GLOB_RECURSE VFS_C "${CMAKE_SOURCE_DIR}/main/kernel/vfs/*.c")
    list(APPEND KERNEL_SOURCES ${VFS_C})
endif ()

if (CFG_ENABLE_CAPS)
    list(APPEND KERNEL_SOURCES "${CMAKE_SOURCE_DIR}/main/kernel/capability/capability.c")
    if (CFG_CAP_DYNAMIC)
        list(APPEND KERNEL_SOURCES "${CMAKE_SOURCE_DIR}/main/kernel/capability/capability_dynamic.c")
    else ()
        list(APPEND KERNEL_SOURCES "${CMAKE_SOURCE_DIR}/main/kernel/capability/capability_static.c")
    endif ()
endif ()

if (CFG_ENABLE_INPUT)
    file(GLOB_RECURSE INPUT_C "${CMAKE_SOURCE_DIR}/main/kernel/io/input.c")
    list(APPEND KERNEL_SOURCES ${INPUT_C})
endif ()

file(GLOB_RECURSE IOPORT_C "${CMAKE_SOURCE_DIR}/main/kernel/io/ioport.c")
list(APPEND KERNEL_SOURCES ${IOPORT_C})

# 库文件
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

# 驱动和架构代码
file(GLOB_RECURSE DRIVERS_C "${CMAKE_SOURCE_DIR}/main/drivers/*.c")
file(GLOB_RECURSE ARCH_C "${ARCH_DIR}/*.c")
file(GLOB_RECURSE ARCH_ASM_LOWER "${ARCH_DIR}/*.s")
file(GLOB_RECURSE ARCH_ASM_UPPER "${ARCH_DIR}/*.S")

if (NOT CFG_TID_DYNAMIC)
    list(FILTER KERNEL_SOURCES EXCLUDE REGEX ".*/tid\\.c$")
endif ()

list(APPEND KERNEL_SOURCES ${LIB_C} ${DRIVERS_C} ${ARCH_C} ${ARCH_ASM_LOWER} ${ARCH_ASM_UPPER})

# ============================================
# 内核目标
# ============================================
add_executable(xnix.elf ${KERNEL_SOURCES})

target_include_directories(xnix.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/main/include
        ${CMAKE_SOURCE_DIR}/main/include/arch
        ${CMAKE_SOURCE_DIR}/main
        ${CMAKE_SOURCE_DIR}/main/lib
        ${CMAKE_BINARY_DIR}/include
        ${ARCH_DIR}/include
)

# CLion 索引
file(GLOB_RECURSE ALL_HEADERS "${CMAKE_SOURCE_DIR}/main/*.h")
target_sources(xnix.elf PRIVATE ${ALL_HEADERS})

# 复制内核到 build 根目录
add_custom_command(TARGET xnix.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:xnix.elf> ${CMAKE_BINARY_DIR}/xnix.elf
)

# 导出内核路径
set(KERNEL_ELF "${CMAKE_BINARY_DIR}/xnix.elf" CACHE INTERNAL "Kernel ELF path")

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
