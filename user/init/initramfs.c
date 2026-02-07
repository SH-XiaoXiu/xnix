/**
 * @file initramfs.c
 * @brief Initramfs 提取器实现
 *
 * 从 FAT12 镜像提取文件到内存文件系统.
 * 目前实现简化的 FAT12 读取,只支持根目录和一级子目录.
 */

#include "initramfs.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xnix/errno.h>

/* FAT12 引导扇区 */
struct fat12_boot_sector {
    uint8_t  jmp[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} __attribute__((packed));

/* FAT 目录项 */
struct fat_dir_entry {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_VOLUME_ID 0x08

/* 读取 FAT 表项 */
static uint16_t fat12_get_next_cluster(const uint8_t *fat, uint16_t cluster) {
    uint32_t offset = cluster + (cluster / 2);
    uint16_t value  = *(uint16_t *)(fat + offset);

    if (cluster & 1) {
        value >>= 4;
    } else {
        value &= 0x0FFF;
    }

    return value;
}

/* 检查是否是文件结束标记 */
static bool fat12_is_eof(uint16_t cluster) {
    return cluster >= 0xFF8;
}

/* 将字符转换为小写 */
static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

/* 将 FAT 文件名转换为普通文件名(小写) */
static void fat_name_to_string(const char *name, const char *ext, char *out, size_t out_size) {
    size_t pos = 0;

    /* 复制文件名部分(转换为小写) */
    for (int i = 0; i < 8 && name[i] != ' '; i++) {
        if (pos < out_size - 1) {
            out[pos++] = to_lower(name[i]);
        }
    }

    /* 如果有扩展名,添加点和扩展名(转换为小写) */
    if (ext[0] != ' ') {
        if (pos < out_size - 1) {
            out[pos++] = '.';
        }
        for (int i = 0; i < 3 && ext[i] != ' '; i++) {
            if (pos < out_size - 1) {
                out[pos++] = to_lower(ext[i]);
            }
        }
    }

    out[pos] = '\0';
}

/* 读取文件内容 */
static int fat12_read_file(struct ramfs_ctx *ctx, const char *path, const uint8_t *img,
                           const struct fat12_boot_sector *bs, const uint8_t *fat,
                           uint16_t first_cluster, uint32_t file_size) {
    /* 创建文件 */
    int fd = ramfs_open(ctx, path, VFS_O_CREAT | VFS_O_WRONLY);
    if (fd < 0) {
        printf("[initramfs] Failed to create file %s: %d\n", path, fd);
        return fd;
    }

    /* 计算数据区起始位置 */
    uint32_t root_dir_sectors =
        ((bs->root_entries * 32) + (bs->bytes_per_sector - 1)) / bs->bytes_per_sector;
    uint32_t first_data_sector =
        bs->reserved_sectors + (bs->num_fats * bs->sectors_per_fat) + root_dir_sectors;

    /* 读取文件数据 */
    uint32_t offset  = 0;
    uint16_t cluster = first_cluster;
    uint8_t  buffer[512];

    while (!fat12_is_eof(cluster) && offset < file_size) {
        /* 计算簇对应的扇区 */
        uint32_t       sector = first_data_sector + (cluster - 2) * bs->sectors_per_cluster;
        const uint8_t *data   = img + sector * bs->bytes_per_sector;

        /* 计算本次读取大小 */
        uint32_t chunk_size = bs->sectors_per_cluster * bs->bytes_per_sector;
        if (offset + chunk_size > file_size) {
            chunk_size = file_size - offset;
        }

        /* 写入 ramfs */
        int ret = ramfs_write(ctx, fd, data, offset, chunk_size);
        if (ret < 0) {
            printf("[initramfs] Write failed for %s: %d\n", path, ret);
            ramfs_close(ctx, fd);
            return ret;
        }

        offset += chunk_size;
        cluster = fat12_get_next_cluster(fat, cluster);
    }

    ramfs_close(ctx, fd);
    return 0;
}

/* 前向声明 */
static int process_cluster_chain(struct ramfs_ctx *ctx, const uint8_t *img,
                                 const struct fat12_boot_sector *bs, const uint8_t *fat,
                                 uint16_t first_cluster, const char *dir_path);

/* 处理目录项 */
static int process_directory(struct ramfs_ctx *ctx, const uint8_t *img,
                             const struct fat12_boot_sector *bs, const uint8_t *fat,
                             const struct fat_dir_entry *entries, uint32_t num_entries,
                             const char *parent_path) {
    for (uint32_t i = 0; i < num_entries; i++) {
        const struct fat_dir_entry *entry = &entries[i];

        /* 跳过空项 */
        if (entry->name[0] == 0x00) {
            break;
        }
        if (entry->name[0] == 0xE5) {
            continue;
        }

        /* 跳过特殊项 */
        if (entry->name[0] == '.') {
            continue;
        }

        /* 跳过卷标 */
        if (entry->attr & FAT_ATTR_VOLUME_ID) {
            continue;
        }

        /* 构建文件名 */
        char filename[32];
        fat_name_to_string(entry->name, entry->ext, filename, sizeof(filename));

        /* 构建完整路径 */
        char fullpath[256];
        if (parent_path[0] == '/' && parent_path[1] == '\0') {
            snprintf(fullpath, sizeof(fullpath), "/%s", filename);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", parent_path, filename);
        }

        /* 处理目录 */
        if (entry->attr & FAT_ATTR_DIRECTORY) {
            printf("[initramfs] Creating directory: %s\n", fullpath);
            int ret = ramfs_mkdir(ctx, fullpath);
            if (ret < 0 && ret != -EEXIST) {
                printf("[initramfs] mkdir failed: %d\n", ret);
                return ret;
            }

            /* 递归处理子目录 */
            uint16_t first_cluster = entry->first_cluster_low;
            if (first_cluster >= 2) {
                ret = process_cluster_chain(ctx, img, bs, fat, first_cluster, fullpath);
                if (ret < 0) {
                    return ret;
                }
            }
            continue;
        }

        /* 处理文件 */
        printf("[initramfs] Extracting file: %s (%u bytes)\n", fullpath, entry->file_size);
        uint16_t first_cluster = entry->first_cluster_low;
        if (first_cluster == 0 || entry->file_size == 0) {
            /* 空文件,只创建 */
            int fd = ramfs_open(ctx, fullpath, VFS_O_CREAT | VFS_O_WRONLY);
            if (fd >= 0) {
                ramfs_close(ctx, fd);
            }
            continue;
        }

        int ret = fat12_read_file(ctx, fullpath, img, bs, fat, first_cluster, entry->file_size);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

/* 处理簇链(读取目录内容) */
static int process_cluster_chain(struct ramfs_ctx *ctx, const uint8_t *img,
                                 const struct fat12_boot_sector *bs, const uint8_t *fat,
                                 uint16_t first_cluster, const char *dir_path) {
    /* 计算数据区起始位置 */
    uint32_t root_dir_sectors =
        ((bs->root_entries * 32) + (bs->bytes_per_sector - 1)) / bs->bytes_per_sector;
    uint32_t first_data_sector =
        bs->reserved_sectors + (bs->num_fats * bs->sectors_per_fat) + root_dir_sectors;

    /* 遍历簇链 */
    uint16_t cluster = first_cluster;
    while (!fat12_is_eof(cluster)) {
        /* 计算簇对应的扇区 */
        uint32_t       sector       = first_data_sector + (cluster - 2) * bs->sectors_per_cluster;
        const uint8_t *cluster_data = img + sector * bs->bytes_per_sector;

        /* 处理这个簇中的目录项 */
        uint32_t entries_per_cluster        = (bs->sectors_per_cluster * bs->bytes_per_sector) / 32;
        const struct fat_dir_entry *entries = (const struct fat_dir_entry *)cluster_data;

        int ret = process_directory(ctx, img, bs, fat, entries, entries_per_cluster, dir_path);
        if (ret < 0) {
            return ret;
        }

        /* 获取下一个簇 */
        cluster = fat12_get_next_cluster(fat, cluster);
    }

    return 0;
}

int initramfs_extract(struct ramfs_ctx *ctx, const void *img_addr, uint32_t img_size) {
    const uint8_t *img = img_addr;

    printf("[initramfs] Extracting from FAT12 image (%u bytes)\n", img_size);

    /* 读取引导扇区 */
    const struct fat12_boot_sector *bs = (const struct fat12_boot_sector *)img;

    printf("[initramfs] Bytes per sector: %u\n", bs->bytes_per_sector);
    printf("[initramfs] Sectors per cluster: %u\n", bs->sectors_per_cluster);
    printf("[initramfs] Root entries: %u\n", bs->root_entries);

    /* 计算各区域位置 */
    uint32_t fat_offset = bs->reserved_sectors * bs->bytes_per_sector;
    uint32_t root_dir_sectors =
        ((bs->root_entries * 32) + (bs->bytes_per_sector - 1)) / bs->bytes_per_sector;
    uint32_t root_dir_offset =
        fat_offset + (bs->num_fats * bs->sectors_per_fat * bs->bytes_per_sector);

    const uint8_t              *fat      = img + fat_offset;
    const struct fat_dir_entry *root_dir = (const struct fat_dir_entry *)(img + root_dir_offset);

    /* 处理根目录 */
    return process_directory(ctx, img, bs, fat, root_dir, bs->root_entries, "/");
}
