/**
 * @file gdt.c
 * @brief 全局描述符表实现
 * @author XiaoXiu
 * @see https://ysos.gzti.me/
 *
 * GDT 布局 (SMP):
 *   0x00: NULL
 *   0x08: Kernel CS
 *   0x10: Kernel DS
 *   0x18: User CS
 *   0x20: User DS
 *   0x28: TSS0 (BSP)
 *   0x30: TSS1 (AP1)
 *   0x38: TSS2 (AP2)
 *   ...
 */

#include <asm/gdt.h>
#include <asm/tss.h>
#include <xnix/config.h>
#include <xnix/types.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* GDT 条目: 5 个基本条目 + CFG_MAX_CPUS 个 TSS 条目 */
#define GDT_ENTRIES (5 + CFG_MAX_CPUS)

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gdtr;

/* TSS 段起始索引 */
#define GDT_TSS_BASE 5

extern void gdt_load(struct gdt_ptr *ptr);

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[idx].base_low    = base & 0xFFFF;
    gdt[idx].base_mid    = (base >> 16) & 0xFF;
    gdt[idx].base_high   = (base >> 24) & 0xFF;
    gdt[idx].limit_low   = limit & 0xFFFF;
    gdt[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[idx].access      = access;
}

static inline void load_tr(uint16_t seg) {
    asm volatile("ltr %0" : : "r"(seg));
}

/**
 * 获取指定 CPU 的 TSS 段选择子
 */
uint16_t gdt_get_tss_selector(uint32_t cpu_id) {
    if (cpu_id >= CFG_MAX_CPUS) {
        return 0x28; /* fallback to BSP TSS */
    }
    return (GDT_TSS_BASE + cpu_id) * 8;
}

/**
 * 设置指定 CPU 的 TSS 描述符
 */
void gdt_set_tss(uint32_t cpu_id, uint32_t tss_base, uint32_t tss_limit) {
    if (cpu_id >= CFG_MAX_CPUS) {
        return;
    }
    int idx = GDT_TSS_BASE + cpu_id;
    /* TSS 描述符: 0x89 = Present, DPL=0, Type=9 (32-bit TSS Available) */
    gdt_set_entry(idx, tss_base, tss_limit, 0x89, 0x00);
}

void gdt_init(void) {
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint32_t)&gdt;

    /* 0x00: 空描述符 */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* 0x08: 内核代码段 - base=0, limit=4GB, Ring 0, RX */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* 0x10: 内核数据段 - base=0, limit=4GB, Ring 0, RW */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    /* 0x18: 用户代码段 - base=0, limit=4GB, Ring 3, RX */
    /* Access: Present(1)|DPL(11)|S(1)|Type(1010) = 1111 1010 = 0xFA */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

    /* 0x20: 用户数据段 - base=0, limit=4GB, Ring 3, RW */
    /* Access: Present(1)|DPL(11)|S(1)|Type(0010) = 1111 0010 = 0xF2 */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

    /* 初始化 TSS 并设置 TSS 描述符 */
    tss_init();

    /* BSP 的 TSS (CPU 0) */
    uint32_t tss_base, tss_limit;
    tss_get_desc(0, &tss_base, &tss_limit);
    gdt_set_tss(0, tss_base, tss_limit);

    gdt_load(&gdtr);

    /* 加载 BSP 的 Task Register */
    load_tr(gdt_get_tss_selector(0));
}

/**
 * AP 初始化 GDT (AP 启动时调用)
 *
 * AP 共享 GDT, 只需加载 GDT 并设置自己的 TR
 */
void gdt_init_ap(uint32_t cpu_id) {
    /* 加载共享的 GDT */
    gdt_load(&gdtr);

    /* 初始化本 CPU 的 TSS 并设置描述符 */
    tss_init_cpu(cpu_id);

    uint32_t tss_base, tss_limit;
    tss_get_desc(cpu_id, &tss_base, &tss_limit);
    gdt_set_tss(cpu_id, tss_base, tss_limit);

    /* 加载本 CPU 的 Task Register */
    load_tr(gdt_get_tss_selector(cpu_id));
}
