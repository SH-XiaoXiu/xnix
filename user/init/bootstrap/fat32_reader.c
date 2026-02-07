/**
 * @file fat32_reader.c
 * @brief FAT32 只读驱动(简化版,仅用于 init 自举)
 *
 * 限制:
 * - 只读(不支持写入,删除)
 * - 只支持 8.3 短文件名
 * - 不支持长文件名(LFN)
 * - 不支持目录遍历(只支持路径查找)
 */

#include "bootstrap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FAT32 Boot Sector */
struct fat32_bpb {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;     /* FAT12/16 only */
    uint16_t total_sectors_16; /* FAT12/16 only */
    uint8_t  media_descriptor;
    uint16_t sectors_per_fat_16; /* FAT12/16 only */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 extended */
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed));

/* FAT32 目录项 */
struct fat32_direntry {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed));

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LFN       0x0F

#define FAT32_EOC 0x0FFFFFF8
#define FAT32_BAD 0x0FFFFFF7

struct fat32_volume {
    const uint8_t *data;
    size_t         size;
    uint32_t       bytes_per_sector;
    uint32_t       sectors_per_cluster;
    uint32_t       bytes_per_cluster;
    uint32_t       reserved_sectors;
    uint32_t       num_fats;
    uint32_t       sectors_per_fat;
    uint32_t       root_cluster;
    uint32_t       fat_start;  /* FAT 起始扇区 */
    uint32_t       data_start; /* 数据区起始扇区 */
    uint32_t       total_sectors;
};

/* 读取 FAT 项 */
static uint32_t fat32_read_fat(fat32_volume_t *vol, uint32_t cluster) {
    if (cluster < 2 || cluster >= 0x0FFFFFF8) {
        return 0;
    }

    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = vol->fat_start + (fat_offset / vol->bytes_per_sector);
    uint32_t ent_offset = fat_offset % vol->bytes_per_sector;

    if (fat_sector * vol->bytes_per_sector + ent_offset + 4 > vol->size) {
        return 0;
    }

    const uint8_t *sector = vol->data + fat_sector * vol->bytes_per_sector;
    uint32_t       val    = *(const uint32_t *)(sector + ent_offset);

    return val & 0x0FFFFFFF;
}

/* 读取簇数据 */
static const uint8_t *fat32_read_cluster(fat32_volume_t *vol, uint32_t cluster) {
    if (cluster < 2) {
        return NULL;
    }

    uint32_t sector = vol->data_start + (cluster - 2) * vol->sectors_per_cluster;
    uint32_t offset = sector * vol->bytes_per_sector;

    if (offset >= vol->size) {
        return NULL;
    }

    return vol->data + offset;
}

/* 将路径分割为组件 */
#define MAX_COMPONENT_LEN 64
static int split_path(const char *path, char components[][MAX_COMPONENT_LEN], int max_components) {
    int         count = 0;
    const char *p     = path;

    /* 跳过前导 / */
    while (*p == '/') {
        p++;
    }

    while (*p && count < max_components) {
        const char *start = p;
        while (*p && *p != '/') {
            p++;
        }

        size_t len = p - start;
        if (len == 0) {
            break;
        }
        if (len >= MAX_COMPONENT_LEN) {
            len = MAX_COMPONENT_LEN - 1;
        }

        memcpy(components[count], start, len);
        components[count][len] = '\0';
        count++;

        while (*p == '/') {
            p++;
        }
    }

    return count;
}

/* name_to_fat removed - using fat_name_matches with prefix matching instead */

/* 检查 FAT 文件名是否匹配(支持长文件名前缀匹配) */
static bool fat_name_matches(const void *fat_name_ptr, const char *search_name) {
    const char *fat_name = (const char *)fat_name_ptr;

    /* 提取扩展名 */
    const char *dot      = strchr(search_name, '.');
    const char *ext      = dot ? dot + 1 : "";
    size_t      base_len = dot ? (size_t)(dot - search_name) : strlen(search_name);
    size_t      ext_len  = strlen(ext);

    /* 比较扩展名 */
    for (size_t i = 0; i < 3; i++) {
        char fat_ext    = fat_name[8 + i];
        char search_ext = ' ';

        if (i < ext_len) {
            char c     = ext[i];
            search_ext = (c >= 'a' && c <= 'z') ? (c - 32) : c;
        }

        if (fat_ext != search_ext) {
            return false;
        }
    }

    /* 比较主文件名 */
    if (base_len <= 8) {
        /* 短文件名 - 完全匹配 */
        for (size_t i = 0; i < 8; i++) {
            char fat_c    = fat_name[i];
            char search_c = ' ';

            if (i < base_len) {
                char c   = search_name[i];
                search_c = (c >= 'a' && c <= 'z') ? (c - 32) : c;
            }

            if (fat_c != search_c) {
                return false;
            }
        }
    } else {
        /* 长文件名 - 前6个字符匹配 */
        for (size_t i = 0; i < 6; i++) {
            char fat_c    = fat_name[i];
            char c        = search_name[i];
            char search_c = (c >= 'a' && c <= 'z') ? (c - 32) : c;

            if (fat_c != search_c) {
                return false;
            }
        }
        /* 第7个字符应该是 '~' */
        if (fat_name[6] != '~') {
            return false;
        }
    }

    return true;
}

/* 在目录中查找文件 */
static const struct fat32_direntry *fat32_find_in_dir(fat32_volume_t *vol, uint32_t dir_cluster,
                                                      const char *name) {
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC) {
        const uint8_t *data = fat32_read_cluster(vol, cluster);
        if (!data) {
            return NULL;
        }

        const struct fat32_direntry *entries = (const struct fat32_direntry *)data;
        uint32_t entries_per_cluster = vol->bytes_per_cluster / sizeof(struct fat32_direntry);

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            const struct fat32_direntry *ent = &entries[i];

            /* 结束标记 */
            if (ent->name[0] == 0x00) {
                return NULL;
            }

            /* 跳过已删除/LFN/卷标 */
            if ((uint8_t)ent->name[0] == 0xE5) {
                continue;
            }
            if (ent->attr & FAT32_ATTR_LFN) {
                continue;
            }
            if (ent->attr & FAT32_ATTR_VOLUME_ID) {
                continue;
            }

            /* 比较文件名(支持长文件名匹配) */
            if (fat_name_matches(ent->name, name)) {
                return ent;
            }
        }

        cluster = fat32_read_fat(vol, cluster);
    }

    return NULL;
}

/* 读取整个文件到内存 */
static void *fat32_read_file(fat32_volume_t *vol, uint32_t start_cluster, uint32_t file_size) {
    void *buffer = malloc(file_size);
    if (!buffer) {
        return NULL;
    }

    uint32_t cluster = start_cluster;
    uint32_t offset  = 0;

    while (cluster >= 2 && cluster < FAT32_EOC && offset < file_size) {
        const uint8_t *data = fat32_read_cluster(vol, cluster);
        if (!data) {
            free(buffer);
            return NULL;
        }

        uint32_t to_copy = vol->bytes_per_cluster;
        if (offset + to_copy > file_size) {
            to_copy = file_size - offset;
        }

        memcpy((uint8_t *)buffer + offset, data, to_copy);
        offset += to_copy;

        cluster = fat32_read_fat(vol, cluster);
    }

    return buffer;
}

fat32_volume_t *fat32_mount(const void *data, size_t size) {
    if (!data || size < 512) {
        return NULL;
    }

    const struct fat32_bpb *bpb = (const struct fat32_bpb *)data;

    /* 验证签名 */
    if (bpb->bytes_per_sector != 512 && bpb->bytes_per_sector != 1024 &&
        bpb->bytes_per_sector != 2048 && bpb->bytes_per_sector != 4096) {
        printf("[fat32] Invalid bytes_per_sector: %u\n", bpb->bytes_per_sector);
        return NULL;
    }

    fat32_volume_t *vol = calloc(1, sizeof(fat32_volume_t));
    if (!vol) {
        return NULL;
    }

    vol->data                = data;
    vol->size                = size;
    vol->bytes_per_sector    = bpb->bytes_per_sector;
    vol->sectors_per_cluster = bpb->sectors_per_cluster;
    vol->bytes_per_cluster   = vol->bytes_per_sector * vol->sectors_per_cluster;
    vol->reserved_sectors    = bpb->reserved_sectors;
    vol->num_fats            = bpb->num_fats;
    vol->sectors_per_fat     = bpb->sectors_per_fat_32;
    vol->root_cluster        = bpb->root_cluster;

    vol->fat_start  = vol->reserved_sectors;
    vol->data_start = vol->reserved_sectors + vol->num_fats * vol->sectors_per_fat;

    if (bpb->total_sectors_32 != 0) {
        vol->total_sectors = bpb->total_sectors_32;
    } else {
        vol->total_sectors = bpb->total_sectors_16;
    }

    printf("[fat32] Mounted volume: %u sectors, %u bytes/cluster, root_cluster=%u\n",
           vol->total_sectors, vol->bytes_per_cluster, vol->root_cluster);

    return vol;
}

int fat32_open(fat32_volume_t *vol, const char *path, const void **out_data, size_t *out_size) {
    if (!vol || !path || !out_data || !out_size) {
        return -1;
    }

    /* 分割路径 */
    char components[16][MAX_COMPONENT_LEN];
    int  component_count = split_path(path, components, 16);

    if (component_count == 0) {
        return -1;
    }

    /* 从根目录开始查找 */
    uint32_t cluster = vol->root_cluster;

    for (int i = 0; i < component_count; i++) {
        const struct fat32_direntry *ent = fat32_find_in_dir(vol, cluster, components[i]);
        if (!ent) {
            printf("[fat32] Component not found: %s\n", components[i]);
            return -1;
        }

        cluster = ((uint32_t)ent->cluster_high << 16) | ent->cluster_low;

        /* 如果不是最后一个组件,必须是目录 */
        if (i < component_count - 1) {
            if (!(ent->attr & FAT32_ATTR_DIRECTORY)) {
                printf("[fat32] Not a directory: %s\n", components[i]);
                return -1;
            }
        } else {
            /* 最后一个组件,必须是文件 */
            if (ent->attr & FAT32_ATTR_DIRECTORY) {
                printf("[fat32] Is a directory: %s\n", components[i]);
                return -1;
            }

            /* 读取文件 */
            void *file_data = fat32_read_file(vol, cluster, ent->file_size);
            if (!file_data) {
                printf("[fat32] Failed to read file: %s\n", path);
                return -1;
            }

            *out_data = file_data;
            *out_size = ent->file_size;
            return 0;
        }
    }

    return -1;
}

void fat32_close(fat32_volume_t *vol) {
    if (vol) {
        free(vol);
    }
}
