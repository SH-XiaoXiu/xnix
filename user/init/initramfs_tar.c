/**
 * @file initramfs_tar.c
 * @brief Initramfs TAR 格式提取器
 */

#include "initramfs.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xnix/errno.h>

/* TAR header (POSIX ustar format) */
struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
};

#define TAR_BLOCK_SIZE 512
#define TAR_TYPE_FILE  '0'
#define TAR_TYPE_DIR   '5'

/* 解析 8 进制字符串 */
static uint32_t parse_octal(const char *str, size_t len) {
    uint32_t val = 0;
    for (size_t i = 0; i < len && str[i] >= '0' && str[i] <= '7'; i++) {
        val = val * 8 + (str[i] - '0');
    }
    return val;
}

int initramfs_extract(struct ramfs_ctx *ctx, const void *img_addr, uint32_t img_size) {
    const uint8_t *img    = img_addr;
    uint32_t       offset = 0;

    printf("[initramfs] Extracting from TAR archive (%u bytes)\n", img_size);

    while (offset + TAR_BLOCK_SIZE <= img_size) {
        const struct tar_header *hdr = (const struct tar_header *)(img + offset);

        /* 检查是否到达末尾(连续两个空块) */
        if (hdr->name[0] == '\0') {
            break;
        }

        /* 验证魔数 */
        if (strncmp(hdr->magic, "ustar", 5) != 0) {
            printf("[initramfs] Invalid TAR magic at offset %u\n", offset);
            break;
        }

        /* 解析文件大小 */
        uint32_t file_size = parse_octal(hdr->size, sizeof(hdr->size));

        /* 构建完整路径,去掉 ./ 前缀 */
        char        fullpath[256];
        const char *path = hdr->name;

        /* 跳过 ./ 前缀 */
        if (path[0] == '.' && path[1] == '/') {
            path += 2;
        }

        /* 跳过 . */
        if (path[0] == '.' && path[1] == '\0') {
            offset += TAR_BLOCK_SIZE;
            continue;
        }

        /* 构建绝对路径 */
        if (path[0] == '/') {
            snprintf(fullpath, sizeof(fullpath), "%s", path);
        } else {
            snprintf(fullpath, sizeof(fullpath), "/%s", path);
        }

        /* 处理目录 */
        if (hdr->typeflag == TAR_TYPE_DIR || hdr->typeflag == '5') {
            /* 移除末尾的 / */
            size_t len = strlen(fullpath);
            if (len > 1 && fullpath[len - 1] == '/') {
                fullpath[len - 1] = '\0';
            }

            printf("[initramfs] Creating directory: %s\n", fullpath);
            int ret = ramfs_mkdir(ctx, fullpath);
            if (ret < 0 && ret != -EEXIST) {
                printf("[initramfs] mkdir failed: %s\n", strerror(-ret));
                return ret;
            }

            offset += TAR_BLOCK_SIZE;
            continue;
        }

        /* 处理普通文件 */
        if (hdr->typeflag == TAR_TYPE_FILE || hdr->typeflag == '\0') {
            printf("[initramfs] Extracting file: %s (%u bytes)\n", fullpath, file_size);

            /* 创建文件 */
            int fd = ramfs_open(ctx, fullpath, VFS_O_CREAT | VFS_O_WRONLY);
            if (fd < 0) {
                printf("[initramfs] Failed to create file %s: %s\n", fullpath, strerror(-fd));
                return fd;
            }

            /* 写入数据 */
            if (file_size > 0) {
                const uint8_t *data = img + offset + TAR_BLOCK_SIZE;
                int            ret  = ramfs_write(ctx, fd, data, 0, file_size);
                if (ret < 0) {
                    printf("[initramfs] Write failed for %s: %s\n", fullpath, strerror(-ret));
                    ramfs_close(ctx, fd);
                    return ret;
                }
            }

            ramfs_close(ctx, fd);

            /* 跳过文件数据(向上取整到 512 字节) */
            uint32_t data_blocks = (file_size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            offset += TAR_BLOCK_SIZE + data_blocks * TAR_BLOCK_SIZE;
        } else {
            /* 跳过未知类型 */
            printf("[initramfs] Skipping unknown type '%c': %s\n", hdr->typeflag, fullpath);
            uint32_t data_blocks = (file_size + TAR_BLOCK_SIZE - 1) / TAR_BLOCK_SIZE;
            offset += TAR_BLOCK_SIZE + data_blocks * TAR_BLOCK_SIZE;
        }
    }

    printf("[initramfs] Extraction complete\n");
    return 0;
}
