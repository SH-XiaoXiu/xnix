#!/bin/bash
# 构建并以调试模式运行 (等待GDB连接)

cd "$(dirname "$0")/.."

mkdir -p build && cd build
cmake .. && make iso
echo "QEMU 等待 GDB 连接 localhost:1234..."
echo "连接命令: gdb build/xnix.elf -ex 'target remote localhost:1234'"
qemu-system-i386 -cdrom xnix.iso -serial stdio -s -S
