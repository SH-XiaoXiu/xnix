/**
 * @file xnix/displaydev.h
 * @brief 显示设备驱动框架
 *
 * 驱动只需实现 display_ops 回调, 框架处理 IPC 收发和协议解析.
 */

#ifndef XNIX_DISPLAYDEV_H
#define XNIX_DISPLAYDEV_H

#include <stddef.h>
#include <stdint.h>
#include <xnix/abi/handle.h>

struct display_device;

/**
 * 显示设备操作回调
 */
struct display_ops {
    /** 写入文本. 返回实际写入字节数或负 errno */
    int (*write)(struct display_device *dev, const void *buf, size_t len);

    /** 清屏. 返回 0 或负 errno */
    int (*clear)(struct display_device *dev);

    /** 滚动 lines 行 (正=向上). 返回 0 或负 errno */
    int (*scroll)(struct display_device *dev, int lines);

    /** 设置光标位置 (可选). 返回 0 或负 errno */
    int (*set_cursor)(struct display_device *dev, int row, int col);

    /** 设置颜色. attr = fg | (bg << 4). 返回 0 或负 errno */
    int (*set_color)(struct display_device *dev, uint8_t attr);

    /** 重置为默认颜色. 返回 0 或负 errno */
    int (*reset_color)(struct display_device *dev);
};

/**
 * 显示设备描述
 */
struct display_device {
    const char        *name;      /* 设备名 (如 "fbcon") */
    int                instance;  /* 实例号 */
    struct display_ops *ops;      /* 操作回调 */
    uint32_t           caps;      /* DISPDEV_CAP_COLOR 等 */
    int                rows;      /* 文本行数 */
    int                cols;      /* 文本列数 */
    void              *priv;      /* 驱动私有数据 */

    handle_t           endpoint;  /* 预设 endpoint 或 HANDLE_INVALID */
};

/**
 * 注册显示设备
 *
 * @return 0 成功, -1 失败
 */
#define DISPLAY_DEVICE_INIT { .endpoint = HANDLE_INVALID }

int displaydev_register(struct display_device *dev);

#endif /* XNIX_DISPLAYDEV_H */
