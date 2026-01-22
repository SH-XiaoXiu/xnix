/**
 * @file idt.h
 * @brief IDT 描述符
 * @author XiaoXiu
 * @date 2026-01-22
 */

#ifndef ARCH_IDT_H
#define ARCH_IDT_H

#include <arch/types.h>

/* IDT 门类型 */
#define IDT_GATE_INTERRUPT  0x8E  /* P=1, DPL=0, 32位中断门 */
#define IDT_GATE_TRAP       0x8F  /* P=1, DPL=0, 32位陷阱门 */

/* IDT 条目数量 */
#define IDT_ENTRIES 256

/**
 * @brief IDT 门描述符
 */
struct idt_entry {
    uint16_t base_low;    /* 处理函数地址低16位 */
    uint16_t selector;    /* 代码段选择子 */
    uint8_t  zero;        /* 保留，必须为0 */
    uint8_t  flags;       /* 类型和属性 */
    uint16_t base_high;   /* 处理函数地址高16位 */
} __attribute__((packed));

/**
 * @brief IDTR 寄存器结构
 */
struct idt_ptr {
    uint16_t limit;       /* IDT 大小 - 1 */
    uint32_t base;        /* IDT 基地址 */
} __attribute__((packed));

/**
 * @brief 初始化 IDT
 */
void idt_init(void);

/**
 * @brief 设置 IDT 门
 * @param num 中断号
 * @param base 处理函数地址
 * @param selector 代码段选择子
 * @param flags 门类型和属性
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);

#endif
