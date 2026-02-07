#!/bin/bash
# Xnix 可引导磁盘模板生成脚本
#
# 用法: ./scripts/make_disk_template.sh [SIZE_MB] [OUTPUT]
#
# 生成包含 MBR、GRUB、FAT32 分区的完整可引导磁盘镜像
# 此镜像将被打包进 ISO，供 installer 程序使用

set -e

# 默认参数
SIZE_MB=${1:-128}
OUTPUT=${2:-build/disk_template.img}
BUILD_DIR=$(dirname "$OUTPUT")

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
    local missing=0
    for cmd in dd mkfs.fat grub-install losetup; do
        if ! command -v $cmd &>/dev/null; then
            error "缺少依赖: $cmd (apt install grub-pc-bin dosfstools)"
            missing=1
        fi
    done
    [ $missing -eq 1 ] && exit 1
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
    echo ""       # 默认起始扇区
    echo ""       # 默认结束扇区
    echo "a"      # 设置启动标志
    echo "t"      # 修改分区类型
    echo "c"      # FAT32 LBA (0x0C)
    echo "w"      # 写入并退出
} | fdisk "$OUTPUT" >/dev/null 2>&1 || true

# 设置 loop 设备
section "挂载 loop 设备"
LOOP=$(sudo losetup -f)
sudo losetup -P "$LOOP" "$OUTPUT"
PART="${LOOP}p1"

# 等待分区设备就绪
sleep 1
[ -b "$PART" ] || error "分区设备不存在: $PART"

# 格式化为 FAT32
section "格式化 FAT32 文件系统"
sudo mkfs.fat -F 32 -n "XNIX" "$PART" >/dev/null

# 挂载分区
MOUNT_POINT=$(mktemp -d)
info "挂载到: $MOUNT_POINT"
sudo mount "$PART" "$MOUNT_POINT"

# 创建目录结构
section "创建目录结构"
sudo mkdir -p "$MOUNT_POINT/boot/grub"
sudo mkdir -p "$MOUNT_POINT/bin"

# 复制内核和模块
section "复制系统文件"
sudo cp "$BUILD_DIR/xnix.elf" "$MOUNT_POINT/boot/" && info "  xnix.elf"
sudo cp "$BUILD_DIR/init.elf" "$MOUNT_POINT/boot/" && info "  init.elf"
sudo cp "$BUILD_DIR/initramfs.img" "$MOUNT_POINT/boot/" && info "  initramfs.img"
sudo cp "$BUILD_DIR/system.img" "$MOUNT_POINT/boot/" && info "  system.img"

# 复制用户程序
section "复制用户程序"
copied=0
for elf in "$BUILD_DIR"/*.elf; do
    [ -f "$elf" ] || continue
    name=$(basename "$elf")
    case "$name" in
        xnix.elf|init.elf) continue ;;
        *)
            sudo cp "$elf" "$MOUNT_POINT/bin/"
            info "  $name"
            copied=$((copied + 1))
            ;;
    esac
done
info "共复制 $copied 个程序"

# 生成 GRUB 配置
section "生成 GRUB 配置"
sudo tee "$MOUNT_POINT/boot/grub/grub.cfg" >/dev/null <<'GRUBCFG'
set timeout=3
set default=0

menuentry "Xnix Operating System" {
    echo "Loading Xnix kernel..."
    multiboot /boot/xnix.elf
    module /boot/init.elf
    module /boot/initramfs.img
    module /boot/system.img
    boot
}

menuentry "Xnix (Safe Mode)" {
    echo "Loading Xnix kernel (safe mode)..."
    multiboot /boot/xnix.elf safe
    module /boot/init.elf
    module /boot/initramfs.img
    module /boot/system.img
    boot
}
GRUBCFG

# 安装 GRUB
section "安装 GRUB bootloader"
if sudo grub-install --target=i386-pc --boot-directory="$MOUNT_POINT/boot" "$LOOP" 2>&1 | grep -i error; then
    warn "GRUB 安装遇到警告，尝试强制安装..."
    sudo grub-install --target=i386-pc --boot-directory="$MOUNT_POINT/boot" --force "$LOOP" >/dev/null 2>&1
fi
info "GRUB 安装完成"

# 同步并卸载
section "同步并卸载"
sync
sudo umount "$MOUNT_POINT"
sudo losetup -d "$LOOP"
rmdir "$MOUNT_POINT"

# 显示结果
section "磁盘模板生成完成"
ACTUAL_SIZE=$(du -h "$OUTPUT" | cut -f1)
info "文件: $OUTPUT"
info "大小: $ACTUAL_SIZE"
info ""
info "此镜像将被打包进 ISO，供 installer 使用"

echo ""
echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}  测试方法${NC}"
echo -e "${CYAN}========================================${NC}"
echo "  QEMU:  qemu-system-i386 -m 128M -hda $OUTPUT"
echo "  dd:    sudo dd if=$OUTPUT of=/dev/sdX bs=4M status=progress"
echo ""
