#!/bin/bash
# 发布版本构建脚本
# 移除调试信息、符号表，启用优化

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-release"
STRIP="strip"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# 清理选项
if [[ "$1" == "clean" ]]; then
    info "清理发布构建目录..."
    rm -rf "$BUILD_DIR"
    info "清理完成"
    exit 0
fi

# 检查工具
command -v cmake >/dev/null 2>&1 || error "需要 cmake"
command -v make >/dev/null 2>&1 || error "需要 make"
command -v grub-mkrescue >/dev/null 2>&1 || warn "grub-mkrescue 未找到，ISO 生成可能失败"

info "创建发布构建目录: $BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

info "配置 CMake (Release 模式)..."
cmake "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCFG_DEBUG=OFF

info "编译内核和用户程序..."
make -j"$(nproc)"

info "剥离符号表..."

# 剥离内核 ELF
if [[ -f xnix.elf ]]; then
    # 保留一份带符号的版本用于调试
    cp xnix.elf xnix.elf.debug
    $STRIP --strip-all xnix.elf
    info "  xnix.elf: $(stat -c%s xnix.elf) bytes (符号保存在 xnix.elf.debug)"
fi

# 剥离用户程序
for elf in init.elf *.elf; do
    [[ -f "$elf" ]] || continue
    [[ "$elf" == "xnix.elf" ]] && continue
    [[ "$elf" == "*.elf" ]] && continue
    [[ "$elf" == *".debug" ]] && continue

    cp "$elf" "${elf}.debug"
    $STRIP --strip-all "$elf"
    info "  $elf: $(stat -c%s "$elf") bytes"
done

info "生成发布 ISO..."
make iso

# 显示最终文件大小
echo ""
info "发布版本构建完成！"
echo ""
echo "产物:"
if [[ -f xnix.iso ]]; then
    echo "  ISO:    $BUILD_DIR/xnix.iso ($(stat -c%s xnix.iso) bytes)"
fi
if [[ -f xnix.elf ]]; then
    echo "  内核:   $BUILD_DIR/xnix.elf ($(stat -c%s xnix.elf) bytes)"
fi
echo ""
echo "调试符号保存在 *.debug 文件中"
echo ""
echo "运行: qemu-system-i386 -cdrom $BUILD_DIR/xnix.iso -serial stdio"
