cmake_minimum_required(VERSION 3.20)
project(Xnix C ASM)

set(CMAKE_C_STANDARD 11)

# ============================================
# 32位 x86 内核编译配置
# ============================================

# C编译选项: 32位, 无标准库, 调试符号
set(CMAKE_C_FLAGS "-m32 -ffreestanding -fno-stack-protector -fno-pie -nostdlib -nostartfiles -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O2")

# ASM编译选项 (GAS语法)
set(CMAKE_ASM_FLAGS "-m32")

# 链接选项
set(CMAKE_EXE_LINKER_FLAGS "-m32 -T${CMAKE_SOURCE_DIR}/kernel/kernel.ld -nostdlib -no-pie")

# ============================================
# 内核目标
# ============================================

# 源文件
set(KERNEL_SOURCES
        boot/multiboot.s
        kernel/kernel.c
)

# 内核ELF文件 (包含调试符号，用于GDB调试)
add_executable(xnix.elf ${KERNEL_SOURCES})

# ============================================
# 创建可引导ISO镜像
# ============================================

set(ISO_DIR ${CMAKE_BINARY_DIR}/isodir)

add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/xnix.iso
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ISO_DIR}/boot/grub
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:xnix.elf> ${ISO_DIR}/boot/xnix.elf
        COMMAND ${CMAKE_COMMAND} -E echo "set timeout=0" > ${ISO_DIR}/boot/grub/grub.cfg
        COMMAND ${CMAKE_COMMAND} -E echo "set default=0" >> ${ISO_DIR}/boot/grub/grub.cfg
        COMMAND ${CMAKE_COMMAND} -E echo "menuentry \\\"Xnix\\\" { multiboot /boot/xnix.elf }" >> ${ISO_DIR}/boot/grub/grub.cfg
        COMMAND grub-mkrescue -o ${CMAKE_BINARY_DIR}/xnix.iso ${ISO_DIR} 2>/dev/null || true
        DEPENDS xnix.elf
        COMMENT "创建可引导ISO..."
)

add_custom_target(iso DEPENDS ${CMAKE_BINARY_DIR}/xnix.iso)

# ============================================
# QEMU运行/调试目标
# ============================================

# 运行QEMU (使用ISO)
add_custom_target(run
        COMMAND qemu-system-i386 -cdrom ${CMAKE_BINARY_DIR}/xnix.iso
        DEPENDS iso
        COMMENT "在QEMU中运行Xnix..."
        VERBATIM
)

# 以调试模式运行QEMU (等待GDB连接)
add_custom_target(debug
        COMMAND qemu-system-i386 -cdrom ${CMAKE_BINARY_DIR}/xnix.iso -s -S
        DEPENDS iso
        COMMENT "在QEMU调试模式下运行Xnix (等待GDB连接端口1234)..."
        VERBATIM
)
