#ifndef ARCH_X86_TSS_H
#define ARCH_X86_TSS_H

#include <xnix/types.h>

/* TSS 结构定义 */
struct tss_entry {
    uint32_t prev_tss; /* 上一个 TSS 指针 */
    uint32_t esp0;     /* Ring 0 栈指针 */
    uint32_t ss0;      /* Ring 0 栈段选择子 */
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

/* 初始化所有 TSS (BSP 调用) */
void tss_init(void);

/* 初始化指定 CPU 的 TSS (AP 调用) */
void tss_init_cpu(uint32_t cpu_id);

/* 更新当前 CPU 的内核栈指针 (上下文切换时调用) */
void tss_set_stack(uint32_t ss0, uint32_t esp0);

/* 更新指定 CPU 的内核栈指针 */
void tss_set_stack_cpu(uint32_t cpu_id, uint32_t ss0, uint32_t esp0);

/* 获取指定 CPU 的 TSS 结构的地址和大小 (供 GDT 初始化使用) */
void tss_get_desc(uint32_t cpu_id, uint32_t *base, uint32_t *limit);

/* 获取指定 CPU 的 TSS 段选择子 */
uint16_t gdt_get_tss_selector(uint32_t cpu_id);

/* 设置指定 CPU 的 TSS 描述符 */
void gdt_set_tss(uint32_t cpu_id, uint32_t tss_base, uint32_t tss_limit);

/* AP 初始化 GDT */
void gdt_init_ap(uint32_t cpu_id);

#endif
