#include <arch/hal/chipset.h>
#include <arch/hal/feature.h>

#include <asm/smp_defs.h>
#include <xnix/string.h>

/* 全局特性变量 */
struct hal_features g_hal_features = {0};

/* SMP 信息 (由 lapic.c 定义) */
extern struct smp_info g_smp_info;

/*
 * x86 CPUID 检测辅助函数
 */
static inline void cpuid(uint32_t code, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(code));
}

void hal_probe_features(struct hal_features *features) {
    if (!features) {
        return;
    }

    memset(features, 0, sizeof(struct hal_features));

    uint32_t eax, ebx, ecx, edx;

    /* 检测 CPU 厂商 */
    cpuid(0, &eax, &ebx, &ecx, &edx);

    /* 厂商字符串拼接: EBX-EDX-ECX */
    uint32_t vendor[4];
    vendor[0] = ebx;
    vendor[1] = edx;
    vendor[2] = ecx;
    vendor[3] = 0;

    strncpy(features->cpu_vendor, (char *)vendor, 15);
    features->cpu_vendor[15] = '\0';

    /* 检测特性标志 (CPUID EAX=1) */
    if (eax >= 1) {
        cpuid(1, &eax, &ebx, &ecx, &edx);

        /* EDX Bit 0: FPU */
        if (edx & (1 << 0)) {
            features->flags |= HAL_FEATURE_FPU;
        }

        /* EDX Bit 9: APIC */
        if (edx & (1 << 9)) {
            features->flags |= HAL_FEATURE_APIC;
        }

        /* 假设 x86 保护模式下 MMU 总是存在的 (虽然严格来说 CPUID PAE 等位也是 MMU 特性) */
        features->flags |= HAL_FEATURE_MMU;
    }

    /* 先更新全局变量,供 mp_table_parse() 中的 hal_has_feature() 使用 */
    g_hal_features.flags = features->flags;

/* 解析 MP Table 获取 SMP 信息 */
#ifdef ENABLE_SMP
    if (acpi_madt_parse(&g_smp_info) == 0 && g_smp_info.cpu_count > 1) {
        features->flags |= HAL_FEATURE_ACPI;
        features->flags |= HAL_FEATURE_SMP;
        features->cpu_count = g_smp_info.cpu_count;
    } else if (mp_table_parse(&g_smp_info) == 0 && g_smp_info.cpu_count > 1) {
        features->flags |= HAL_FEATURE_SMP;
        features->cpu_count = g_smp_info.cpu_count;
    } else {
        features->cpu_count = 1;
    }
#else
    /* 非 SMP 构建也解析 MP Table 以获取 APIC 信息 */
    if (acpi_madt_parse(&g_smp_info) == 0) {
        features->flags |= HAL_FEATURE_ACPI;
    } else {
        mp_table_parse(&g_smp_info);
    }
    features->cpu_count = 1;
#endif

    /* 内存探测 (通常由 Multiboot 传递,这里仅做占位) */
    features->ram_size_mb = 0; // 需要从 Multiboot info 获取

    /* 更新全局副本 */
    memcpy(&g_hal_features, features, sizeof(struct hal_features));

    // pr_debug("HAL: Detected CPU Vendor: %s", features->cpu_vendor);
    // pr_debug("HAL: Features: MMU=%d, FPU=%d, APIC=%d",
    //        !!(features->flags & HAL_FEATURE_MMU),
    //        !!(features->flags & HAL_FEATURE_FPU),
    //        !!(features->flags & HAL_FEATURE_APIC));
}

/*
 * Chipset Ops 桩实现
 */
static const struct hal_chipset_ops *g_chipset_ops = NULL;

const struct hal_chipset_ops *hal_get_chipset_ops(void) {
    return g_chipset_ops;
}

void hal_register_chipset(const struct hal_chipset_ops *ops) {
    g_chipset_ops = ops;
    if (ops && ops->init) {
        ops->init();
    }
}
