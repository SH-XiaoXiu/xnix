/**
 * @file block.c
 * @brief 块设备抽象层实现
 */

#include <block.h>
#include <string.h>
#include <stdio.h>

/* 块设备表 */
static struct block_device block_devices[BLOCK_MAX_DEVICES];
static int block_device_count = 0;

/* 类型名称表 */
static const char *type_names[] = {
    [BLOCK_DEV_ATA]     = "ATA",
    [BLOCK_DEV_SCSI]    = "SCSI",
    [BLOCK_DEV_VIRTIO]  = "VirtIO",
    [BLOCK_DEV_RAMDISK] = "RAMDisk",
    [BLOCK_DEV_UNKNOWN] = "Unknown"
};

const char *block_type_name(block_dev_type_t type) {
    if (type >= 0 && type < sizeof(type_names) / sizeof(type_names[0])) {
        return type_names[type];
    }
    return "Unknown";
}

int block_alloc_name(block_dev_type_t type, char *name_out, int name_size) {
    if (!name_out || name_size < 4) {
        return -1;
    }

    /* 找到该类型已注册的设备数量 */
    int type_count = 0;
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        if (block_devices[i].valid && block_devices[i].type == type) {
            type_count++;
        }
    }

    /* 生成名称: sda, sdb, sdc... */
    char suffix = 'a' + type_count;
    if (suffix > 'z') {
        return -1;  /* 超出范围 */
    }

    snprintf(name_out, name_size, "sd%c", suffix);
    return 0;
}

int block_register(struct block_device *dev) {
    if (!dev || !dev->ops) {
        return -1;
    }

    /* 查找空闲槽位 */
    int slot = -1;
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        if (!block_devices[i].valid) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        return -1;  /* 设备表已满 */
    }

    /* 如果没有名称，自动分配 */
    if (dev->name[0] == '\0') {
        if (block_alloc_name(dev->type, dev->name, sizeof(dev->name)) < 0) {
            return -1;
        }
    }

    /* 检查名称冲突 */
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        if (block_devices[i].valid &&
            strncmp(block_devices[i].name, dev->name, sizeof(dev->name)) == 0) {
            return -1;  /* 名称已存在 */
        }
    }

    /* 获取设备信息 */
    if (dev->ops->get_info) {
        dev->ops->get_info(dev->driver_ctx, &dev->info);
    }

    dev->valid = true;
    block_devices[slot] = *dev;
    block_device_count++;

    return 0;
}

int block_unregister(struct block_device *dev) {
    if (!dev) {
        return -1;
    }

    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        if (block_devices[i].valid && &block_devices[i] == dev) {
            block_devices[i].valid = false;
            block_device_count--;
            return 0;
        }
    }

    return -1;
}

struct block_device *block_find(const char *name) {
    if (!name) {
        return NULL;
    }

    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        if (block_devices[i].valid &&
            strncmp(block_devices[i].name, name, sizeof(block_devices[i].name)) == 0) {
            return &block_devices[i];
        }
    }

    return NULL;
}

struct block_device *block_first(void) {
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        if (block_devices[i].valid) {
            return &block_devices[i];
        }
    }
    return NULL;
}

struct block_device *block_next(struct block_device *dev) {
    if (!dev) {
        return NULL;
    }

    int start_idx = -1;
    for (int i = 0; i < BLOCK_MAX_DEVICES; i++) {
        if (&block_devices[i] == dev) {
            start_idx = i;
            break;
        }
    }

    if (start_idx < 0) {
        return NULL;
    }

    for (int i = start_idx + 1; i < BLOCK_MAX_DEVICES; i++) {
        if (block_devices[i].valid) {
            return &block_devices[i];
        }
    }

    return NULL;
}

int block_count(void) {
    return block_device_count;
}
