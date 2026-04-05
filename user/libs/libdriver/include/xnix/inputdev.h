/**
 * @file xnix/inputdev.h
 * @brief 输入设备驱动框架
 *
 * 驱动只需实现 input_ops 回调, 框架处理 IPC 收发和协议解析.
 */

#ifndef XNIX_INPUTDEV_H
#define XNIX_INPUTDEV_H

#include <stdint.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/input.h>

struct input_device;

/**
 * 输入设备操作回调
 */
struct input_ops {
    /** 阻塞读取一个输入事件. 返回 0 成功, 负 errno 失败 */
    int (*read_event)(struct input_device *dev, struct input_event *ev);

    /** 非阻塞查询待处理事件数. 返回事件数 (>=0) */
    int (*poll)(struct input_device *dev);
};

/**
 * 输入设备描述
 */
struct input_device {
    const char       *name;      /* 设备名 (如 "ps2_kbd") */
    int               instance;  /* 实例号 */
    struct input_ops *ops;       /* 操作回调 */
    uint32_t          caps;      /* INPUTDEV_CAP_KEY | INPUTDEV_CAP_MOUSE */
    uint32_t          type;      /* INPUTDEV_TYPE_KEYBOARD 等 */
    void             *priv;      /* 驱动私有数据 */

    /* --- 框架内部 --- */
    handle_t          endpoint;
};

/**
 * 注册输入设备
 *
 * @return 0 成功, -1 失败
 */
int inputdev_register(struct input_device *dev);

#endif /* XNIX_INPUTDEV_H */
