#!/bin/bash
#
# install_bin.sh - 从源镜像复制到 Xnix 运行时镜像
#
# 用法:
#   ./scripts/install_bin.sh <src> [dst]
#
# 参数:
#   src     源镜像文件（如 dev.vhd）
#   dst     目标镜像文件（可选，默认: build/xnix_disk.img）
#
# 工作流:
#   1. 在 Windows 创建并挂载一个 VHD (如 E:\)
#   2. 将编译好的 .elf 文件复制到 E:\bin\
#   3. 卸载 VHD，然后运行:
#      ./scripts/install_bin.sh /path/to/dev.vhd
#
#   Xnix 每次启动都会使用这个干净的镜像副本
#
# 示例:
#   ./scripts/install_bin.sh dev.vhd                    # 自动创建 build/xnix_disk.img
#   ./scripts/install_bin.sh dev.vhd custom_disk.img    # 指定目标路径
#

set -e

# 获取项目根目录（脚本在 scripts/ 下）
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
DEFAULT_DST="$BUILD_DIR/xnix_disk.img"

SRC="${1:-}"
DST="${2:-}"

# 检查源参数
if [ -z "$SRC" ]; then
    cat << EOF
用法: $0 <src> [dst]

从源镜像复制到 Xnix 运行时镜像

参数:
  src     源镜像文件（必需）
  dst     目标镜像文件（可选，默认: build/xnix_disk.img）

示例:
  $0 dev.vhd                    # 自动创建 build/xnix_disk.img
  $0 dev.vhd custom_disk.img    # 指定目标路径

推荐工作流:
  1. 在 Windows 创建并挂载 VHD
  2. 将 *.elf 文件复制到挂载盘的 /bin/
  3. 卸载 VHD，然后运行此脚本
  4. 使用 ./run --hda build/xnix_disk.img 启动
EOF
    exit 1
fi

# 检查源文件是否存在
if [ ! -f "$SRC" ]; then
    echo "错误: 源文件不存在: $SRC"
    exit 1
fi

# 如果没有指定目标，使用默认路径
if [ -z "$DST" ]; then
    DST="$DEFAULT_DST"
    echo "目标路径未指定，使用默认: $DST"
fi

# 确保目标目录存在
DST_DIR="$(dirname "$DST")"
if [ ! -d "$DST_DIR" ]; then
    echo "创建目录: $DST_DIR"
    mkdir -p "$DST_DIR"
fi

# 获取源文件大小
SRC_SIZE=$(stat -c%s "$SRC" 2>/dev/null || stat -f%z "$SRC" 2>/dev/null)
SRC_SIZE_MB=$((SRC_SIZE / 1024 / 1024))

echo "源文件: $SRC ($SRC_SIZE_MB MB)"
echo "目标文件: $DST"

# 如果目标已存在，显示覆盖提示
if [ -f "$DST" ]; then
    echo "目标文件已存在，将被覆盖"
fi

# 复制镜像
echo "复制镜像..."
cp "$SRC" "$DST"

# 验证
DST_SIZE=$(stat -c%s "$DST" 2>/dev/null || stat -f%z "$DST" 2>/dev/null)
if [ "$SRC_SIZE" = "$DST_SIZE" ]; then
    echo "完成! 镜像大小: $SRC_SIZE_MB MB"
else
    echo "警告: 文件大小不匹配 (源: $SRC_SIZE, 目标: $DST_SIZE)"
    exit 1
fi

echo ""
echo "使用方法:"
echo "  ./run --hda $DST"
echo "  或: ./run --install $SRC  # 每次运行前自动复制"
