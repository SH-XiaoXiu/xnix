#ifndef ARCH_HAL_FEATURE_H
#define ARCH_HAL_FEATURE_H

#include <xnix/types.h>

/**
 * @brief 硬件特性位掩码
 */
#define HAL_FEATURE_MMU  (1 << 0) /* 内存管理单元 */
#define HAL_FEATURE_FPU  (1 << 1) /* 浮点运算单元 */
#define HAL_FEATURE_SMP  (1 << 2) /* 对称多处理 */
#define HAL_FEATURE_APIC (1 << 3) /* 高级可编程中断控制器 */
#define HAL_FEATURE_ACPI (1 << 4) /* 高级配置与电源接口 */
#define HAL_FEATURE_VIRT (1 << 5) /* 硬件虚拟化支持 */

/**
 * @brief 硬件特性探测结构体
 */
struct hal_features {
    uint32_t flags;          /* 特性位掩码 */
    uint32_t cpu_count;      /* 探测到的 CPU 数量 */
    uint32_t ram_size_mb;    /* 探测到的内存大小 (MB) */
    char     cpu_vendor[16]; /* CPU 厂商字符串 */
    char     cpu_model[48];  /* CPU 型号字符串 */
};

/* 全局 HAL 特性 */
extern struct hal_features g_hal_features;

/**
 * @brief 探测硬件特性
 *
 * 在内核启动早期调用,用于决定加载哪些子系统.
 *
 * @param features 输出参数,填充探测到的特性
 */
void hal_probe_features(struct hal_features *features);

void hal_probe_smp_late(void);

void hal_force_disable_smp(void);

/**
 * @brief 检查特定特性是否支持
 */
static inline bool hal_has_feature(uint32_t feature_mask) {
    return (g_hal_features.flags & feature_mask) != 0;
}

#endif /* ARCH_HAL_FEATURE_H */
