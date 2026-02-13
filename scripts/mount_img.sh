#!/bin/bash
# mount_img.sh - 挂载磁盘镜像到 /media/xnix (读写)
#
# 用法:
#   ./scripts/mount_img.sh <img>       挂载
#   ./scripts/mount_img.sh -u          卸载
#

set -e

MOUNT_POINT="/media/xnix"
IMG="${1:-}"

if [ "$IMG" = "-u" ]; then
    sudo umount "$MOUNT_POINT" 2>/dev/null && echo "已卸载 $MOUNT_POINT" || echo "未挂载"
    exit 0
fi

if [ -z "$IMG" ]; then
    echo "用法: $0 <img>    挂载镜像"
    echo "      $0 -u       卸载"
    exit 1
fi

if [ ! -f "$IMG" ]; then
    echo "错误: $IMG 不存在"
    exit 1
fi

# 检测分区偏移
OFFSET=0
FILE_TYPE=$(file -b "$IMG")
if echo "$FILE_TYPE" | grep -q "MBR boot sector"; then
    SECTOR=$(fdisk -l "$IMG" 2>/dev/null | awk '/^[^ ]*\.img/ {print $2; exit}')
    if [ -n "$SECTOR" ] && [ "$SECTOR" != "*" ]; then
        OFFSET=$((SECTOR * 512))
    else
        SECTOR=$(fdisk -l "$IMG" 2>/dev/null | awk '/^[^ ]*\.img/ {print $3; exit}')
        if [ -n "$SECTOR" ]; then
            OFFSET=$((SECTOR * 512))
        fi
    fi
    echo "MBR 分区表，偏移: $OFFSET 字节"
fi

sudo mkdir -p "$MOUNT_POINT"
sudo mount -o loop,rw,offset=$OFFSET,uid=$(id -u),gid=$(id -g),umask=0000 "$IMG" "$MOUNT_POINT"
echo "已挂载 $IMG -> $MOUNT_POINT (读写)"
