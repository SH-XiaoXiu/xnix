#!/bin/bash
# 创建 FAT 磁盘镜像

set -e

# 默认参数
SIZE_MB=32
OUTPUT="disk.img"
MOUNT_DIR=""

show_help() {
    cat << EOF
创建 FAT16 磁盘镜像

用法: $0 [选项]

选项:
  -s, --size <MB>     镜像大小（默认: 32MB）
  -o, --output <file> 输出文件名（默认: disk.img）
  -d, --dir <path>    复制目录内容到镜像
  -h, --help          显示此帮助

示例:
  $0                          # 创建 32MB 空镜像
  $0 -s 64 -o fat.img         # 创建 64MB 镜像
  $0 -d testfiles/            # 创建镜像并复制文件
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
            MOUNT_DIR="$2"
            shift 2
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

echo "创建 ${SIZE_MB}MB FAT16 磁盘镜像: $OUTPUT"

# 创建空镜像
dd if=/dev/zero of="$OUTPUT" bs=1M count=$SIZE_MB status=progress

# 格式化为 FAT16
mkfs.fat -F 16 -n "XNIX" "$OUTPUT"

echo "镜像创建完成: $OUTPUT"

# 如果指定了目录，复制文件
if [ -n "$MOUNT_DIR" ] && [ -d "$MOUNT_DIR" ]; then
    echo "复制文件从: $MOUNT_DIR"

    # 使用 mtools 复制文件（不需要 root 权限）
    if command -v mcopy &> /dev/null; then
        export MTOOLS_SKIP_CHECK=1
        mcopy -i "$OUTPUT" -s "$MOUNT_DIR"/* ::
        echo "文件复制完成"
    else
        echo "提示: 安装 mtools 可自动复制文件 (apt install mtools)"
    fi
fi

echo "完成！使用方法: ./run --hda $OUTPUT"
