/**
 * @file mp_table.c
 * @brief MP Table (Intel MultiProcessor Specification) 解析
 *
 * 解析 MP 浮点结构和配置表,获取 CPU 数量和 APIC 信息
 */

#include <asm/apic.h>
#include <asm/smp_defs.h>

#include <arch/hal/feature.h>
#include <xnix/debug.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/* BIOS 搜索区域 */
#define EBDA_PTR_ADDR   0x40E  /* EBDA 指针地址 */
#define BIOS_ROM_START  0xE0000
#define BIOS_ROM_END    0xFFFFF

/**
 * 计算校验和
 */
static uint8_t checksum(const void *data, size_t len) {
    uint8_t sum = 0;
    const uint8_t *p = data;
    for (size_t i = 0; i < len; i++) {
        sum += p[i];
    }
    return sum;
}

/**
 * 在指定内存范围内搜索 MP 浮点结构
 *
 * @param start 起始地址
 * @param end   结束地址
 * @return MP 浮点结构指针, 未找到返回 NULL
 */
static const struct mp_fps *mp_search_range(paddr_t start, paddr_t end) {
    /* 搜索步长为 16 字节 (MP 规范要求) */
    for (paddr_t addr = start; addr < end; addr += 16) {
        const struct mp_fps *fps = (const struct mp_fps *)addr;

        /* 检查签名 "_MP_" */
        if (fps->signature != MP_FPS_SIGNATURE) {
            continue;
        }

        /* 验证长度 */
        if (fps->length != 1) { /* 长度应为 1 (16 字节) */
            continue;
        }

        /* 验证校验和 */
        if (checksum(fps, 16) != 0) {
            continue;
        }

        return fps;
    }
    return NULL;
}

/**
 * 搜索 MP 浮点结构
 *
 * 搜索顺序:
 * 1. EBDA (Extended BIOS Data Area) 的前 1KB
 * 2. 系统 BIOS ROM (0xE0000 - 0xFFFFF)
 */
static const struct mp_fps *mp_find_fps(void) {
    const struct mp_fps *fps = NULL;

    /* 尝试从 EBDA 搜索 */
    uint16_t ebda_seg = *(uint16_t *)EBDA_PTR_ADDR;
    if (ebda_seg) {
        paddr_t ebda_base = (paddr_t)ebda_seg << 4;
        fps = mp_search_range(ebda_base, ebda_base + 1024);
        if (fps) {
            return fps;
        }
    }

    /* 从 BIOS ROM 区域搜索 */
    fps = mp_search_range(BIOS_ROM_START, BIOS_ROM_END);
    return fps;
}

/**
 * 解析 MP 配置表
 */
static int mp_parse_config(const struct mp_config *cfg, struct smp_info *info) {
    /* 验证签名 */
    if (cfg->signature != MP_CFG_SIGNATURE) {
        pr_err("MP: Invalid config table signature");
        return -1;
    }

    /* 验证校验和 */
    if (checksum(cfg, cfg->length) != 0) {
        pr_err("MP: Config table checksum failed");
        return -1;
    }

    /* 获取 LAPIC 基地址 */
    info->lapic_base = cfg->lapic_addr;
    if (info->lapic_base == 0) {
        info->lapic_base = LAPIC_BASE_DEFAULT;
    }

    pr_debug("MP: OEM='%.8s', Product='%.12s', LAPIC=0x%x",
             cfg->oem_id, cfg->product_id, info->lapic_base);

    /* 遍历配置表条目 */
    const uint8_t *entry = (const uint8_t *)(cfg + 1);
    for (uint16_t i = 0; i < cfg->entry_count; i++) {
        switch (*entry) {
        case MP_ENTRY_PROCESSOR: {
            const struct mp_processor *proc = (const struct mp_processor *)entry;
            if ((proc->flags & MP_PROC_ENABLED) && info->cpu_count < CFG_MAX_CPUS) {
                uint32_t cpu_id = info->cpu_count;
                info->lapic_ids[cpu_id] = proc->lapic_id;

                if (proc->flags & MP_PROC_BSP) {
                    info->bsp_id = cpu_id;
                    pr_debug("MP: CPU%u (BSP): LAPIC_ID=%u", cpu_id, proc->lapic_id);
                } else {
                    pr_debug("MP: CPU%u (AP):  LAPIC_ID=%u", cpu_id, proc->lapic_id);
                }
                info->cpu_count++;
            }
            entry += sizeof(struct mp_processor);
            break;
        }

        case MP_ENTRY_BUS: {
            const struct mp_bus *bus = (const struct mp_bus *)entry;
            pr_debug("MP: Bus %u: '%.6s'", bus->bus_id, bus->bus_type);
            entry += sizeof(struct mp_bus);
            break;
        }

        case MP_ENTRY_IOAPIC: {
            const struct mp_ioapic *ioapic = (const struct mp_ioapic *)entry;
            if (ioapic->flags & MP_IOAPIC_ENABLED) {
                info->ioapic_base = ioapic->addr;
                info->ioapic_id = ioapic->id;
                pr_debug("MP: IOAPIC %u at 0x%x", ioapic->id, ioapic->addr);
            }
            entry += sizeof(struct mp_ioapic);
            break;
        }

        case MP_ENTRY_IOINT: {
            entry += sizeof(struct mp_ioint);
            break;
        }

        case MP_ENTRY_LINT: {
            entry += sizeof(struct mp_lint);
            break;
        }

        default:
            pr_warn("MP: Unknown entry type %u", *entry);
            return -1;
        }
    }

    return 0;
}

int mp_table_parse(struct smp_info *info) {
    if (!info) {
        return -1;
    }

    /* 初始化默认值 */
    memset(info, 0, sizeof(*info));
    info->cpu_count = 1;
    info->bsp_id = 0;
    info->lapic_base = LAPIC_BASE_DEFAULT;
    info->ioapic_base = IOAPIC_BASE_DEFAULT;
    info->apic_available = false;

    /* 检查 CPUID 是否支持 APIC */
    if (!hal_has_feature(HAL_FEATURE_APIC)) {
        pr_warn("MP: APIC not supported by CPU");
        return 0; /* 返回单核配置 */
    }

    /* 搜索 MP 浮点结构 */
    const struct mp_fps *fps = mp_find_fps();
    if (!fps) {
        pr_debug("MP: No MP table found, assuming single CPU");
        /* 即使没有 MP Table, 如果 CPUID 报告支持 APIC, 仍可使用 */
        info->lapic_ids[0] = 0;
        info->apic_available = true;
        return 0;
    }

    pr_debug("MP: Found MP FPS at 0x%x, spec rev 1.%u",
             (uint32_t)fps, fps->spec_rev);

    /* 检查是否使用默认配置 */
    if (fps->features[0] != 0) {
        /* 使用默认配置表 (简化处理) */
        pr_debug("MP: Using default config type %u", fps->features[0]);
        info->cpu_count = 2; /* 默认配置假设 2 个 CPU */
        info->lapic_ids[0] = 0;
        info->lapic_ids[1] = 1;
        info->apic_available = true;
        return 0;
    }

    /* 解析 MP 配置表 */
    if (fps->config_ptr == 0) {
        pr_warn("MP: No config table pointer");
        info->apic_available = true;
        return 0;
    }

    const struct mp_config *cfg = (const struct mp_config *)fps->config_ptr;
    if (mp_parse_config(cfg, info) != 0) {
        pr_err("MP: Failed to parse config table");
        return -1;
    }

    info->apic_available = true;
    return 0;
}

void mp_table_dump(const struct smp_info *info) {
    if (!info) {
        return;
    }

    kprintf("SMP: %u CPU%s detected", info->cpu_count, info->cpu_count > 1 ? "s" : "");
    if (info->cpu_count > 1) {
        kprintf(" (BSP=CPU%u)\n", info->bsp_id);
    } else {
        kprintf("\n");
    }

    for (uint32_t i = 0; i < info->cpu_count; i++) {
        kprintf("  CPU%u: LAPIC_ID=%u%s\n",
                i, info->lapic_ids[i],
                i == info->bsp_id ? " [BSP]" : "");
    }

    if (info->apic_available) {
        kprintf("  LAPIC base:  0x%08x\n", info->lapic_base);
        kprintf("  IOAPIC base: 0x%08x (ID=%u)\n", info->ioapic_base, info->ioapic_id);
    }
}
