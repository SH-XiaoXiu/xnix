#ifndef PLAT_PLATFORM_H
#define PLAT_PLATFORM_H

#include <arch/hal/feature.h>

struct early_device;

/**
 * @brief 平台描述符
 *
 * 定义平台的早期设备和初始化流程
 */
struct platform_desc {
    const char *name;                     /* 平台名称 */
    const struct early_device *devices;   /* 早期设备列表 */

    /* 早期初始化 (在 core 初始化后调用) */
    int (*early_init)(void);

    /* 驱动初始化 */
    int (*driver_init)(void);
};

/**
 * @brief 获取当前平台
 */
const struct platform_desc *platform_get(void);

#endif /* PLAT_PLATFORM_H */
