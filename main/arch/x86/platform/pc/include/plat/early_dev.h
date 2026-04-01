#ifndef PLAT_EARLY_DEV_H
#define PLAT_EARLY_DEV_H

/**
 * @brief 早期设备描述
 *
 * 定义平台需要的早期驱动
 */
struct early_device {
    const char *name;       /* 设备名称 */
    int priority;           /* 初始化优先级 (0 最高) */

    /* 探测设备是否可用 (可选) */
    int (*probe)(void);

    /* 初始化/注册设备 */
    void (*init)(void);
};

/**
 * @brief 按优先级初始化所有已探测的设备
 *
 * @param devices 设备列表 (以 NULL 结尾)
 * @return 0 成功, 负数失败
 */
int early_devices_init(const struct early_device *devices);

#endif /* PLAT_EARLY_DEV_H */
