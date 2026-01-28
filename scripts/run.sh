#!/bin/bash
# 构建并运行 Xnix

cd "$(dirname "$0")/.."

mkdir -p build && cd build
cmake .. && make iso
qemu-system-i386 -cdrom xnix.iso -serial stdio -no-reboot -smp 4
