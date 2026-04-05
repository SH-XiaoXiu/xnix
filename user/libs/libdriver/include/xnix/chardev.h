/**
 * @file xnix/chardev.h
 * @brief 字符设备驱动框架
 *
 * 驱动只需实现 char_ops 回调, 框架处理 IPC 收发和协议解析.
 *
 * 用法:
 *   struct char_device dev = {
 *       .name = "com", .instance = 0,
 *       .ops = &my_ops, .caps = CHARDEV_CAP_READ | CHARDEV_CAP_WRITE,
 *       .priv = &my_context,
 *   };
 *   chardev_register(&dev);
 */

#ifndef XNIX_CHARDEV_H
#define XNIX_CHARDEV_H

#include <stddef.h>
#include <stdint.h>
#include <xnix/abi/handle.h>

struct char_device;

/**
 * 字符设备操作回调
 *
 * 所有回调在设备的服务线程中调用, 不需要驱动自行加锁 (除非多设备共享硬件).
 * 返回值: 正数=字节数, 0=无数据, 负数=errno.
 */
struct char_ops {
    /** 阻塞读取, 至少返回 1 字节. 返回实际读取字节数或负 errno */
    int (*read)(struct char_device *dev, void *buf, size_t max);

    /** 写入数据. 返回实际写入字节数或负 errno */
    int (*write)(struct char_device *dev, const void *buf, size_t len);

    /** 设备控制 (可选). args[0..5] 对应 data[2..7]. 返回 0 或负 errno */
    int (*ioctl)(struct char_device *dev, uint32_t cmd, const uint32_t *args);
};

/**
 * 字符设备描述
 *
 * 驱动填写 name/instance/ops/caps/priv, 其余由框架填写.
 */
struct char_device {
    const char      *name;      /* 设备名 (如 "com") */
    int              instance;  /* 实例号 (如 0 表示 COM1) */
    struct char_ops *ops;       /* 操作回调 */
    uint32_t         caps;      /* CHARDEV_CAP_READ | CHARDEV_CAP_WRITE */
    void            *priv;      /* 驱动私有数据 */
    handle_t         endpoint;  /* 预设 init 注入的 endpoint, 或 HANDLE_INVALID 由框架创建 */
};

/**
 * 注册字符设备
 *
 * 创建 IPC endpoint 并启动服务线程. endpoint 名格式: "{name}{instance}"
 * 如果 endpoint 已存在 (如 init 注入的 "serial"), 使用现有 handle.
 *
 * @param dev  驱动填写的设备描述, 框架会填写 endpoint 字段
 * @return 0 成功, -1 失败
 */
/** 静态初始化器, 确保 endpoint = HANDLE_INVALID */
#define CHAR_DEVICE_INIT { .endpoint = HANDLE_INVALID }

int chardev_register(struct char_device *dev);

#endif /* XNIX_CHARDEV_H */
