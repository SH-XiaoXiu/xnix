/**
 * @file gdt.c
 * @brief 全局描述符表实现
 * @author XiaoXiu
 */

#include "gdt.h"

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

static struct gdt_entry gdt[3];
static struct gdt_ptr gdtr;

extern void gdt_load(struct gdt_ptr *ptr);

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[idx].base_low    = base & 0xFFFF;
    gdt[idx].base_mid    = (base >> 16) & 0xFF;
    gdt[idx].base_high   = (base >> 24) & 0xFF;
    gdt[idx].limit_low   = limit & 0xFFFF;
    gdt[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[idx].access      = access;
}

void gdt_init(void) {
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint32_t)&gdt;

    /* 0x00: 空描述符 */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* 0x08: 内核代码段 - base=0, limit=4GB, 可执行/可读 */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

    /* 0x10: 内核数据段 - base=0, limit=4GB, 可读/可写 */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

    gdt_load(&gdtr);
}
