/**
 * @file block.h
 * @brief 块设备抽象层
 *
 * 提供统一的块设备接口，支持 ATA、SCSI、VirtIO 等不同类型磁盘。
 *
 * 设备命名规范:
 *   - sda, sdb, sdc... 表示 SCSI/SATA/ATA 磁盘
 *   - sda1, sda2... 表示分区
 *
 * 使用示例:
 *   struct block_device *dev = block_find("sda");
 *   if (dev) {
 *       char buf[512];
 *       dev->ops->read(dev->driver_ctx, 0, 1, buf);
 *   }
 */

#ifndef XNIX_LIBBLOCK_H
#define XNIX_LIBBLOCK_H

#include <stdint.h>
#include <stdbool.h>

/* 最大块设备数量 */
#define BLOCK_MAX_DEVICES 16

/* 块设备类型 */
typedef enum {
    BLOCK_DEV_ATA,      /**< ATA/IDE 磁盘 */
    BLOCK_DEV_SCSI,     /**< SCSI 磁盘 */
    BLOCK_DEV_VIRTIO,   /**< VirtIO 磁盘 */
    BLOCK_DEV_RAMDISK,  /**< 内存磁盘 */
    BLOCK_DEV_UNKNOWN   /**< 未知类型 */
} block_dev_type_t;

/* 块设备信息 */
struct block_info {
    uint64_t sector_count;      /**< 扇区总数 */
    uint32_t sector_size;       /**< 扇区大小（字节） */
    uint32_t flags;             /**< 标志位 */
    block_dev_type_t type;      /**< 设备类型 */
    char model[40];             /**< 设备型号 */
    char serial[20];            /**< 序列号 */
};

/* 块设备标志 */
#define BLOCK_FLAG_REMOVABLE    (1 << 0)  /**< 可移动设备 */
#define BLOCK_FLAG_READONLY     (1 << 1)  /**< 只读设备 */

/* 块设备操作 */
struct block_ops {
    /**
     * 读取扇区
     * @param ctx 驱动上下文
     * @param lba 起始 LBA
     * @param count 扇区数量
     * @param buffer 输出缓冲区
     * @return 0 成功，负数错误码
     */
    int (*read)(void *ctx, uint64_t lba, uint32_t count, void *buffer);

    /**
     * 写入扇区
     * @param ctx 驱动上下文
     * @param lba 起始 LBA
     * @param count 扇区数量
     * @param buffer 输入缓冲区
     * @return 0 成功，负数错误码
     */
    int (*write)(void *ctx, uint64_t lba, uint32_t count, const void *buffer);

    /**
     * 刷新缓存
     * @param ctx 驱动上下文
     * @return 0 成功，负数错误码
     */
    int (*flush)(void *ctx);

    /**
     * 获取设备信息
     * @param ctx 驱动上下文
     * @param info 输出信息结构
     * @return 0 成功，负数错误码
     */
    int (*get_info)(void *ctx, struct block_info *info);
};

/* 块设备实例 */
struct block_device {
    char name[16];              /**< 设备名称 (sda, sdb, etc.) */
    block_dev_type_t type;      /**< 设备类型 */
    struct block_ops *ops;      /**< 操作函数表 */
    void *driver_ctx;           /**< 驱动私有上下文 */
    struct block_info info;     /**< 设备信息缓存 */
    bool valid;                 /**< 是否有效 */
};

/**
 * 注册块设备
 * @param dev 设备实例（name 必须已设置）
 * @return 0 成功，负数错误码
 *
 * 设备名称会自动分配（如果冲突则使用下一个可用名称）
 * 例如：第一个 ATA 设备会命名为 sda
 */
int block_register(struct block_device *dev);

/**
 * 注销块设备
 * @param dev 设备实例
 * @return 0 成功，负数错误码
 */
int block_unregister(struct block_device *dev);

/**
 * 查找块设备
 * @param name 设备名称 (sda, sdb, etc.)
 * @return 设备指针，未找到返回 NULL
 */
struct block_device *block_find(const char *name);

/**
 * 获取第一个块设备（用于遍历）
 * @return 第一个设备指针，无设备返回 NULL
 */
struct block_device *block_first(void);

/**
 * 获取下一个块设备（用于遍历）
 * @param dev 当前设备
 * @return 下一个设备指针，无更多设备返回 NULL
 */
struct block_device *block_next(struct block_device *dev);

/**
 * 获取块设备数量
 * @return 已注册的设备数量
 */
int block_count(void);

/**
 * 获取设备类型名称
 * @param type 设备类型
 * @return 类型名称字符串
 */
const char *block_type_name(block_dev_type_t type);

/**
 * 分配下一个可用设备名称
 * @param type 设备类型
 * @param name_out 输出缓冲区
 * @param name_size 缓冲区大小
 * @return 0 成功，负数错误码
 */
int block_alloc_name(block_dev_type_t type, char *name_out, int name_size);

#endif /* XNIX_LIBBLOCK_H */
