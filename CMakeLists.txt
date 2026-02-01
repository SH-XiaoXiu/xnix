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

# ============================================
# 编译裁切：子系统开关（仅控制编译，不生成宏）
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
# 编译裁切：类型大小配置
# ============================================
# 资源受限配置可使用 16 位，默认 32 位
set(CFG_TID_BITS 32 CACHE STRING "TID type bits (16 or 32)")
set(CFG_PRIORITY_BITS 32 CACHE STRING "Priority type bits (8 or 32)")
set(CFG_SLICE_BITS 32 CACHE STRING "Time slice type bits (16 or 32)")

# ============================================
# 编译裁切：资源分配模式（细粒度控制）
# ============================================
# 每个资源类型可独立选择静态/动态模式：
# - 静态模式(OFF)：使用固定大小，满了返回失败，内存占用确定
# - 动态模式(ON)：初始小容量，满了自动扩容，适合通用场景

# --- 线程 ID 分配器 ---
option(CFG_TID_DYNAMIC "Enable dynamic TID allocation" ON)
set(CFG_MAX_THREADS 4096 CACHE STRING "Thread table size")
# CFG_TID_DYNAMIC=ON  时：CFG_MAX_THREADS 是初始容量，满了会扩容
# CFG_TID_DYNAMIC=OFF 时：CFG_MAX_THREADS 是固定最大值，满了返回失败

# --- 进程 ID 分配器 ---
option(CFG_PID_DYNAMIC "Enable dynamic PID allocation" ON)
set(CFG_MAX_PROCESSES 1024 CACHE STRING "Process table size")
# CFG_PID_DYNAMIC=ON  时：CFG_MAX_PROCESSES 是初始容量
# CFG_PID_DYNAMIC=OFF 时：CFG_MAX_PROCESSES 是固定最大值

# --- 能力表 ---
option(CFG_CAP_DYNAMIC "Enable dynamic capability table" ON)
# 能力表初始大小由 CFG_CAP_TABLE_SIZE 控制（见 IPC 配置）

# --- 通用位图分配器 ---
option(CFG_BITMAP_DYNAMIC "Enable dynamic bitmap allocator" ON)

# ============================================
# 调度器配置
# ============================================
set(CFG_SCHED_HZ 100 CACHE STRING "Scheduler tick frequency (Hz)")
set(CFG_DEF_TIME_SLICE 10 CACHE STRING "Default thread time slice (ticks)")

# ============================================
# 内存配置
# ============================================
# 线程栈大小（编译时固定，影响线程内存占用）
set(CFG_THREAD_STACK_SIZE 4096 CACHE STRING "Thread kernel stack size (bytes)")

# 内核直接映射大小（高半核架构）
# 物理内存 [0, CFG_KERNEL_DIRECT_MAP_MB] 映射到虚拟地址 [0xC0000000, ...]
# 建议值：嵌入式 16-32MB，通用 64MB，大内存系统 128-256MB
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

configure_file(
        ${CMAKE_SOURCE_DIR}/main/include/xnix/config.h.in
        ${CMAKE_BINARY_DIR}/include/xnix/config.h
)

# ============================================
# 源文件收集（模块化）
# ============================================

# 必需子系统（始终编译）
set(KERNEL_REQUIRED_DIRS boot mm sched irq sys process hal exec udm)
set(KERNEL_SOURCES "")

# 内核入口文件（在 kernel 根目录）
list(APPEND KERNEL_SOURCES "${CMAKE_SOURCE_DIR}/main/kernel/xnix.c")

foreach (SUBDIR ${KERNEL_REQUIRED_DIRS})
    file(GLOB_RECURSE SUBDIR_C "${CMAKE_SOURCE_DIR}/main/kernel/${SUBDIR}/*.c")
    list(APPEND KERNEL_SOURCES ${SUBDIR_C})
endforeach ()

# 可选子系统：开启时编译真实实现（覆盖弱符号）
if (CFG_ENABLE_IPC)
    file(GLOB_RECURSE IPC_C "${CMAKE_SOURCE_DIR}/main/kernel/ipc/*.c")
    list(APPEND KERNEL_SOURCES ${IPC_C})
endif ()

if (CFG_ENABLE_VFS)
    file(GLOB_RECURSE VFS_C "${CMAKE_SOURCE_DIR}/main/kernel/vfs/*.c")
    list(APPEND KERNEL_SOURCES ${VFS_C})
endif ()

if (CFG_ENABLE_CAPS)
    # Capability 子系统：始终编译公共代码
    list(APPEND KERNEL_SOURCES "${CMAKE_SOURCE_DIR}/main/kernel/capability/capability.c")
    # 根据动态开关选择实现
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

# I/O 子系统（ioport 是必需的，input 是可选的）
file(GLOB_RECURSE IOPORT_C "${CMAKE_SOURCE_DIR}/main/kernel/io/ioport.c")
list(APPEND KERNEL_SOURCES ${IOPORT_C})

# 库文件（递归收集所有，然后排除静态/动态版本，按需添加回来）
file(GLOB_RECURSE LIB_C "${CMAKE_SOURCE_DIR}/main/lib/*.c")
# 排除静态/动态版本文件，分别处理
list(FILTER LIB_C EXCLUDE REGEX ".*_static\\.c$")
list(FILTER LIB_C EXCLUDE REGEX ".*_dynamic\\.c$")

# 位图分配器：根据配置选择静态或动态版本
if (CFG_BITMAP_DYNAMIC)
    list(APPEND LIB_C "${CMAKE_SOURCE_DIR}/main/lib/resource_dynamic.c")
else ()
    list(APPEND LIB_C "${CMAKE_SOURCE_DIR}/main/lib/resource_static.c")
endif ()

# ID 分配器：根据配置选择静态或动态版本
if (NOT CFG_TID_DYNAMIC)
    # 静态模式：使用 lib/id_alloc_static.c 的弱符号
    list(APPEND LIB_C "${CMAKE_SOURCE_DIR}/main/lib/id_alloc_static.c")
endif ()

# 驱动和架构代码
file(GLOB_RECURSE DRIVERS_C "${CMAKE_SOURCE_DIR}/main/drivers/*.c")
file(GLOB_RECURSE ARCH_C "${ARCH_DIR}/*.c")
file(GLOB_RECURSE ARCH_ASM_LOWER "${ARCH_DIR}/*.s")
file(GLOB_RECURSE ARCH_ASM_UPPER "${ARCH_DIR}/*.S")

# ============================================
# 扩展性裁切：细粒度动态/静态选择
# ============================================

# TID 分配器（动态版本在 kernel/sched/tid.c）
if (NOT CFG_TID_DYNAMIC)
    # 静态模式：排除内核中的动态版本
    list(FILTER KERNEL_SOURCES EXCLUDE REGEX ".*/tid\\.c$")
endif ()

# PID 分配器（当前 process.c 中包含，后续可分离）
# if(NOT CFG_PID_DYNAMIC)
#     list(FILTER KERNEL_SOURCES EXCLUDE REGEX ".*/pid_dynamic\\.c$")
# endif()

# 合并所有源文件
list(APPEND KERNEL_SOURCES ${LIB_C} ${DRIVERS_C} ${ARCH_C} ${ARCH_ASM_LOWER} ${ARCH_ASM_UPPER})

# 显示配置信息
message(STATUS "Subsystem configuration:")
message(STATUS "  IPC: ${CFG_ENABLE_IPC}")
message(STATUS "  VFS: ${CFG_ENABLE_VFS}")
message(STATUS "  Capability: ${CFG_ENABLE_CAPS}")
message(STATUS "  Input: ${CFG_ENABLE_INPUT}")
message(STATUS "Dynamic allocation:")
message(STATUS "  TID: ${CFG_TID_DYNAMIC}")
message(STATUS "  PID: ${CFG_PID_DYNAMIC}")
message(STATUS "  CAP: ${CFG_CAP_DYNAMIC}")
message(STATUS "  Bitmap: ${CFG_BITMAP_DYNAMIC}")

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
# 动态添加 UDM 驱动
foreach (DRIVER_ELF ${DRIVER_TARGETS})
    string(APPEND GRUB_CFG_CONTENT "  module /boot/${DRIVER_ELF}\n")
endforeach ()
# 动态添加应用程序
foreach (APP_ELF ${APP_TARGETS})
    string(APPEND GRUB_CFG_CONTENT "  module /boot/${APP_ELF}\n")
endforeach ()
# 动态添加 demo 程序
foreach (DEMO_ELF ${DEMO_TARGETS})
    string(APPEND GRUB_CFG_CONTENT "  module /boot/${DEMO_ELF}\n")
endforeach ()
string(APPEND GRUB_CFG_CONTENT "}\n")
file(MAKE_DIRECTORY ${ISO_DIR}/boot/grub)
file(WRITE ${ISO_DIR}/boot/grub/grub.cfg "${GRUB_CFG_CONTENT}")

# 构建依赖和复制命令列表
set(ISO_DEPENDS xnix.elf init.elf)
set(ISO_COPY_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:xnix.elf> ${ISO_DIR}/boot/xnix.elf
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/init.elf ${ISO_DIR}/boot/init.elf
)

# 添加 UDM 驱动
foreach (DRIVER_ELF ${DRIVER_TARGETS})
    list(APPEND ISO_DEPENDS ${DRIVER_ELF})
    list(APPEND ISO_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${DRIVER_ELF} ${ISO_DIR}/boot/${DRIVER_ELF}
    )
endforeach ()

# 添加应用程序
foreach (APP_ELF ${APP_TARGETS})
    list(APPEND ISO_DEPENDS ${APP_ELF})
    list(APPEND ISO_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${APP_ELF} ${ISO_DIR}/boot/${APP_ELF}
    )
endforeach ()

# 添加 demo 程序
foreach (DEMO_ELF ${DEMO_TARGETS})
    list(APPEND ISO_DEPENDS ${DEMO_ELF})
    list(APPEND ISO_COPY_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/${DEMO_ELF} ${ISO_DIR}/boot/${DEMO_ELF}
    )
endforeach ()

add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/xnix.iso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_DIR}/boot/grub
        ${ISO_COPY_COMMANDS}
        COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/xnix.iso ${ISO_DIR} 2>/dev/null || true
        DEPENDS ${ISO_DEPENDS}
        COMMENT "创建可引导 ISO..."
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
