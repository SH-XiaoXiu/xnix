#!/bin/bash
# 构建并运行 Xnix

cd "$(dirname "$0")/.."

mkdir -p build && cd build
cmake .. && make iso
