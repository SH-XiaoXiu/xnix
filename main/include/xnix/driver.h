/**
 * @file driver.h
 * @brief 驱动注册与选择框架
 *
 * 支持 Boot 裁切:驱动自注册 + 优先级选择 + 命令行偏好.
 * 所有驱动都编译,启动时根据硬件特性和命令行自动选择.
 */

#ifndef XNIX_DRIVER_H
#define XNIX_DRIVER_H

#include <xnix/types.h>

/*
 * IRQ 控制器驱动框架
 */

struct irqchip_driver {
    const char *name;
    int         priority; /* 优先级,数值越大越优先 */

    /**
     * 探测硬件是否支持此驱动
     * @return true 表示可用
     */
    bool (*probe)(void);

    /* 驱动操作接口 */
    void (*init)(void);
    void (*enable)(uint8_t irq);
    void (*disable)(uint8_t irq);
    void (*eoi)(uint8_t irq);

    /* 链表指针(内部使用) */
    struct irqchip_driver *next;
};

/**
 * 注册 IRQ 控制器驱动
 * 驱动在 arch_early_init() 时调用此函数注册自己
 */
void irqchip_register(struct irqchip_driver *drv);

/**
 * 选择最佳 IRQ 控制器
 *
 * @param prefer 命令行偏好的驱动名称,可为 NULL
 * @return 选中的驱动,失败返回 NULL
 */
struct irqchip_driver *irqchip_select(const char *prefer);

/**
 * 选择并初始化 IRQ 控制器
 *
 * @param prefer 命令行偏好的驱动名称,可为 NULL
 */
void irqchip_select_and_init(const char *prefer);

/**
 * 获取当前活动的 IRQ 控制器驱动
 */
struct irqchip_driver *irqchip_get_current(void);

/*
 * 定时器驱动框架
 */

struct timer_driver_ext {
    const char *name;
    int         priority; /* 优先级,数值越大越优先 */

    /**
     * 探测硬件是否支持此驱动
     * @return true 表示可用
     */
    bool (*probe)(void);

    /* 驱动操作接口 */
    void (*init)(uint32_t freq);
    uint64_t (*get_ticks)(void);

    /* 链表指针(内部使用) */
    struct timer_driver_ext *next;
};

/**
 * 注册定时器驱动
 * 驱动在 arch_early_init() 时调用此函数注册自己
 */
void timer_drv_register(struct timer_driver_ext *drv);

/**
 * 选择最佳定时器驱动
 *
 * @param prefer 命令行偏好的驱动名称,可为 NULL
 * @return 选中的驱动,失败返回 NULL
 */
struct timer_driver_ext *timer_drv_select(const char *prefer);

/**
 * 选择并初始化定时器驱动
 * 此函数只选择驱动,不调用 init().实际初始化由 timer_init() 完成.
 *
 * @param prefer 命令行偏好的驱动名称,可为 NULL
 */
void timer_drv_select_best(const char *prefer);

/**
 * 获取当前活动的定时器驱动
 */
struct timer_driver_ext *timer_drv_get_current(void);

/*
 * 命令行参数辅助函数
 */

/**
 * 保存命令行指针供后续查询
 * 由 boot_init() 调用
 */
void boot_save_cmdline(const char *cmdline);

/**
 * 从启动命令行获取字符串值
 *
 * @param key 参数名(如 "xnix.irqchip")
 * @return 参数值,不存在返回 NULL
 *
 * 注意:返回的字符串指向静态缓冲区,不可修改
 */
const char *boot_get_cmdline_value(const char *key);

#endif /* XNIX_DRIVER_H */
