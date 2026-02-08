#!/bin/bash
# 创建 Xnix 外部磁盘镜像 (MBR + FAT16 分区)
#
# 生成的镜像可直接用 ./run --hda 挂载, fatfsd_ata 会自动识别 MBR 分区

set -e

# 默认参数
SIZE_MB=32
OUTPUT="disk.img"
COPY_DIRS=()
RAW=false

show_help() {
    cat << EOF
创建 Xnix FAT16 磁盘镜像

用法: $0 [选项]

选项:
  -s, --size <MB>     镜像大小（默认: 32MB）
  -o, --output <file> 输出文件名（默认: disk.img）
  -d, --dir <path>    复制目录内容到镜像（可多次指定）
  --raw               创建裸 FAT16 镜像（无 MBR 分区表）
  -h, --help          显示此帮助

示例:
  $0                              # 创建 32MB 磁盘镜像
  $0 -s 64 -o fat.img             # 创建 64MB 镜像
  $0 -d build/bin/                 # 创建镜像并复制 bin 工具
  $0 -d build/bin/ -d testfiles/   # 复制多个目录
EOF
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -s|--size)
            SIZE_MB="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT="$2"
            shift 2
            ;;
        -d|--dir)
            COPY_DIRS+=("$2")
            shift 2
            ;;
        --raw)
            RAW=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "未知选项: $1"
            exit 1
            ;;
    esac
done

# 检查依赖
for cmd in dd mkfs.fat mmd mcopy; do
    if ! command -v $cmd &>/dev/null; then
        echo "错误: 缺少依赖: $cmd (apt install mtools dosfstools)"
        exit 1
    fi
done

if ! $RAW && ! command -v fdisk &>/dev/null; then
    echo "错误: 缺少依赖: fdisk"
    exit 1
fi

echo "创建 ${SIZE_MB}MB FAT16 磁盘镜像: $OUTPUT"

# 创建空镜像
dd if=/dev/zero of="$OUTPUT" bs=1M count=$SIZE_MB status=none

if $RAW; then
    # 裸 FAT16 镜像（无分区表）
    mkfs.fat -F 16 -n "XNIX" "$OUTPUT" >/dev/null

    # mtools 配置: 直接访问整个镜像
    MTOOLSRC=$(mktemp)
    cat > "$MTOOLSRC" <<EOF
drive d:
    file="$OUTPUT"
EOF
    export MTOOLSRC
else
    # MBR + FAT16 分区
    {
        echo "o"      # 新建 DOS 分区表
        echo "n"      # 新建分区
        echo "p"      # 主分区
        echo "1"      # 分区号 1
        echo ""       # 默认起始扇区 (2048)
        echo ""       # 默认结束扇区
        echo "t"      # 修改分区类型
        echo "e"      # FAT16 LBA (0x0E)
        echo "w"      # 写入
    } | fdisk "$OUTPUT" >/dev/null 2>&1 || true

    # 获取分区偏移
    PART_START=$(fdisk -l "$OUTPUT" 2>/dev/null | awk '/img1/{print $2}')
    if [ -z "$PART_START" ]; then
        PART_START=2048
    fi
    PART_SECTORS=$((SIZE_MB * 2048 - PART_START))

    # 创建独立分区镜像并格式化, 再写回磁盘 (避免 --offset 兼容性问题)
    PART_IMG=$(mktemp)
    dd if=/dev/zero of="$PART_IMG" bs=512 count=$PART_SECTORS status=none
    mkfs.fat -F 16 -n "XNIX" "$PART_IMG" >/dev/null
    dd if="$PART_IMG" of="$OUTPUT" bs=512 seek=$PART_START conv=notrunc status=none
    rm -f "$PART_IMG"

    # mtools 配置: 通过 partition 访问
    MTOOLSRC=$(mktemp)
    cat > "$MTOOLSRC" <<EOF
drive d:
    file="$OUTPUT"
    partition=1
EOF
    export MTOOLSRC
fi

# 创建 Xnix 标准目录结构
mmd d:/bin d:/etc d:/mnt 2>/dev/null || true

# 复制目录内容
for dir in "${COPY_DIRS[@]}"; do
    if [ ! -d "$dir" ]; then
        echo "警告: 目录不存在, 跳过: $dir"
        continue
    fi
    echo "复制: $dir"
    for f in "$dir"/*; do
        [ -f "$f" ] || continue
        # 推断目标目录: 含 .elf 放 bin/, 否则放根目录
        fname=$(basename "$f")
        if [[ "$fname" == *.elf ]]; then
            mcopy -o "$f" "d:/bin/" 2>/dev/null || true
        else
            mcopy -o "$f" "d:/" 2>/dev/null || true
        fi
    done
done

# 清理
rm -f "$MTOOLSRC"

# 显示结果
ACTUAL_SIZE=$(du -h "$OUTPUT" | cut -f1)
echo "完成: $OUTPUT ($ACTUAL_SIZE)"
echo ""
echo "使用方法:"
echo "  ./run --hda $OUTPUT"
echo "  ./run --install $OUTPUT"
