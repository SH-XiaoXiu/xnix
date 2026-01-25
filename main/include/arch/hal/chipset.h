#ifndef ARCH_HAL_CHIPSET_H
#define ARCH_HAL_CHIPSET_H

#include <xnix/types.h>

/**
 * @brief 芯片组操作接口
 * 定义主板/平台特定的动态操作,如中断控制器初始化,定时器设置等.
 * 这些操作可能在启动时根据 hal_probe_features 的结果动态绑定.
 */
struct hal_chipset_ops {
    const char *name;

    /* 初始化 */
    void (*init)(void);

    /* 中断控制器操作 */
    void (*irq_enable)(uint8_t irq);
    void (*irq_disable)(uint8_t irq);
    void (*irq_eoi)(uint8_t irq);

    /* 定时器操作 */
    void (*timer_init)(uint32_t freq);

    /* SMP 相关 (可选) */
    void (*smp_start_ap)(uint8_t cpu_id, uint32_t entry_point);
    void (*smp_send_ipi)(uint8_t cpu_id, uint8_t vector);
};

/**
 * @brief 获取当前激活的芯片组操作集
 */
const struct hal_chipset_ops *hal_get_chipset_ops(void);

/**
 * @brief 注册芯片组驱动
 *
 * @param ops 芯片组操作集
 */
void hal_register_chipset(const struct hal_chipset_ops *ops);

#endif /* ARCH_HAL_CHIPSET_H */
