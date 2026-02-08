#!/bin/bash
# Xnix 可引导磁盘模板生成脚本
#
# 用法: ./scripts/make_disk_template.sh SIZE_MB OUTPUT BUILD_DIR
#
# 生成包含 MBR、GRUB、FAT32 分区的完整可引导磁盘镜像
# 使用 mtools (无需 sudo/mount)

set -e

SIZE_MB=${1:-128}
OUTPUT=${2:-build/disk_template.img}
BUILD_DIR=${3:-$(dirname "$OUTPUT")}

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${GREEN}[INFO]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }
section() { echo -e "${CYAN}==>${NC} $*"; }

# 检查依赖
check_deps() {
    for cmd in dd fdisk mkfs.fat mmd mcopy grub-mkimage; do
        if ! command -v $cmd &>/dev/null; then
            error "缺少依赖: $cmd"
        fi
    done
}

# 检查必需文件
check_files() {
    [ -f "$BUILD_DIR/xnix.elf" ] || error "内核不存在: $BUILD_DIR/xnix.elf"
    [ -f "$BUILD_DIR/init.elf" ] || error "init 不存在: $BUILD_DIR/init.elf"
    [ -f "$BUILD_DIR/initramfs.img" ] || error "initramfs 不存在"
    [ -f "$BUILD_DIR/system.img" ] || error "system.img 不存在"
}

section "Xnix 可引导磁盘模板生成器"
info "镜像大小: ${SIZE_MB}MB"
info "输出文件: $OUTPUT"
info "构建目录: $BUILD_DIR"

check_deps
check_files

# 清理旧镜像
[ -f "$OUTPUT" ] && rm -f "$OUTPUT"

# 创建空白镜像
section "创建 ${SIZE_MB}MB 空白镜像"
dd if=/dev/zero of="$OUTPUT" bs=1M count=$SIZE_MB status=none

# 创建 MBR 分区表
section "创建 MBR 分区表"
{
    echo "o"      # 创建新的 DOS 分区表
    echo "n"      # 新建分区
    echo "p"      # 主分区
    echo "1"      # 分区号 1
    echo ""       # 默认起始扇区 (2048)
    echo ""       # 默认结束扇区
    echo "a"      # 设置启动标志
    echo "t"      # 修改分区类型
    echo "e"      # FAT16 LBA (0x0E)
    echo "w"      # 写入并退出
} | fdisk "$OUTPUT" >/dev/null 2>&1 || true

# 获取分区偏移
PART_START=$(fdisk -l "$OUTPUT" 2>/dev/null | awk '/^.*\*/{print $3}')
if [ -z "$PART_START" ]; then
    PART_START=2048
fi
PART_OFFSET=$((PART_START * 512))
PART_SECTORS=$((SIZE_MB * 2048 - PART_START))

info "分区起始扇区: $PART_START (偏移 $PART_OFFSET)"

# 格式化分区为 FAT32 (使用 offset)
section "格式化 FAT32 文件系统"
mkfs.fat -F 16 -n "XNIX" --offset $PART_START "$OUTPUT" $PART_SECTORS >/dev/null

# 配置 mtools (使用 offset 访问分区)
MTOOLSRC=$(mktemp)
cat > "$MTOOLSRC" <<EOF
drive d:
    file="$OUTPUT"
    partition=1
EOF
export MTOOLSRC

# 创建目录结构
section "创建目录结构"
mmd d:/boot d:/boot/grub d:/sbin d:/bin d:/etc d:/sys d:/drivers d:/mnt 2>/dev/null || true

# 复制系统文件 (从 system.img 提取)
section "从 system.img 复制文件"

# 配置 mtools 访问 system.img
MTOOLSRC_SYS=$(mktemp)
cat > "$MTOOLSRC_SYS" <<EOF
drive s:
    file="$BUILD_DIR/system.img"
EOF

# 临时目录用于中转
TMPDIR=$(mktemp -d)

# 复制 system.img 内容到磁盘模板
# 使用 mcopy 从 system.img 提取文件到临时目录, 再复制到磁盘模板
for dir in sbin bin etc sys drivers; do
    MTOOLSRC="$MTOOLSRC_SYS" mcopy -s -n "s:/$dir" "$TMPDIR/" 2>/dev/null || true
done

export MTOOLSRC="$MTOOLSRC"
# 再从临时目录复制到磁盘模板
for dir in sbin bin etc sys drivers; do
    if [ -d "$TMPDIR/$dir" ]; then
        for f in "$TMPDIR/$dir"/*; do
            [ -f "$f" ] || continue
            MTOOLSRC="$MTOOLSRC" mcopy -o "$f" "d:/$dir/" 2>/dev/null || true
        done
    fi
done

# 复制 boot 文件
section "复制启动文件"
MTOOLSRC="$MTOOLSRC" mcopy -o "$BUILD_DIR/xnix.elf" "d:/boot/xnix.elf"
MTOOLSRC="$MTOOLSRC" mcopy -o "$BUILD_DIR/init.elf" "d:/boot/init.elf"
MTOOLSRC="$MTOOLSRC" mcopy -o "$BUILD_DIR/initramfs.img" "d:/boot/initramfs.img"
info "  xnix.elf, init.elf, initramfs.img"

# 生成磁盘启动 grub.cfg (无 system 模块)
section "生成 GRUB 配置"
GRUB_CFG=$(mktemp)
cat > "$GRUB_CFG" <<'GRUBCFG'
set timeout=3
set default=0

menuentry "Xnix" {
    multiboot /boot/xnix.elf
    module /boot/init.elf name=init
    module /boot/initramfs.img name=initramfs
    boot
}
GRUBCFG

MTOOLSRC="$MTOOLSRC" mcopy -o "$GRUB_CFG" "d:/boot/grub/grub.cfg"

# 安装 GRUB
section "安装 GRUB bootloader"
GRUB_CORE=$(mktemp)

# 生成 GRUB 核心镜像
grub-mkimage -O i386-pc -o "$GRUB_CORE" \
    -p "(hd0,msdos1)/boot/grub" \
    biosdisk part_msdos fat normal multiboot boot configfile 2>/dev/null

# 写入 GRUB boot.img (MBR 前 440 字节)
GRUB_BOOT="/usr/lib/grub/i386-pc/boot.img"
if [ -f "$GRUB_BOOT" ]; then
    dd if="$GRUB_BOOT" of="$OUTPUT" bs=440 count=1 conv=notrunc status=none
    info "  boot.img 写入 MBR"
else
    warn "grub boot.img 未找到, 跳过 MBR 写入"
fi

# 写入 GRUB 核心镜像 (从扇区 1 开始)
dd if="$GRUB_CORE" of="$OUTPUT" bs=512 seek=1 conv=notrunc status=none
info "  core.img 写入扇区 1+"

# 清理
rm -f "$MTOOLSRC" "$MTOOLSRC_SYS" "$GRUB_CFG" "$GRUB_CORE"
rm -rf "$TMPDIR"

# 显示结果
section "磁盘模板生成完成"
ACTUAL_SIZE=$(du -h "$OUTPUT" | cut -f1)
info "文件: $OUTPUT"
info "大小: $ACTUAL_SIZE"
