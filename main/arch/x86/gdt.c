/**
 * @file gdt.c
 * @brief 全局描述符表实现
 * @author XiaoXiu
 * @see https://ysos.gzti.me/
 */

#include <arch/x86/gdt.h>
#include <arch/x86/tss.h>

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

static struct gdt_entry gdt[6]; /* 增加到 6 个条目 */
static struct gdt_ptr   gdtr;

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

    /* 0x28: TSS 段 */
    tss_init();
    uint32_t tss_base, tss_limit;
    tss_get_desc(&tss_base, &tss_limit);

    gdt_set_entry(5, tss_base, tss_limit, 0x89, 0x00);

    gdt_load(&gdtr);

    /* 加载 Task Register */
    /* Index 5 = 0x28. RPL=0. -> 0x28 */
    load_tr(0x28);
}
