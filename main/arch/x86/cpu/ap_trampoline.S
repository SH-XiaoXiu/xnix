/**
 * @file ap_trampoline.S
 * @brief AP (Application Processor) 启动 trampoline 代码
 *
 * 此代码在启动时被复制到物理地址 AP_TRAMPOLINE_ADDR,
 * AP 核收到 SIPI 后从这里开始执行 (16 位实模式)
 *
 * 执行流程:
 * 1. 16 位实模式: 加载临时 GDT, 开启保护模式
 * 2. 32 位保护模式: 加载正式 GDT, 跳转到 C 入口
 */

#include <asm/smp_asm.h>
#include <xnix/config.h>

/* 汇编中使用的别名 (映射到头文件定义) */
#define LAPIC_BASE       LAPIC_BASE_ADDR
#define LAPIC_ID_REG     LAPIC_ID_OFFSET
#define MSR_IA32_APIC_BASE MSR_APIC_BASE
#define APIC_BASE_X2APIC MSR_APIC_X2APIC
#define APIC_BASE_ENABLE MSR_APIC_ENABLE

.section .text
.code16

/* Trampoline 起始标记 (用于计算代码大小) */
.global ap_trampoline_start
ap_trampoline_start:

/**
 * AP 16 位入口点
 *
 * 此时 CS:IP = AP_TRAMPOLINE_SEG:0x0000 (物理地址 AP_TRAMPOLINE_ADDR)
 * CPU 处于 16 位实模式
 */
.global ap_entry_16
ap_entry_16:
    cli
    cld

    /* 设置数据段 */
    mov $AP_TRAMPOLINE_SEG, %ax
    mov %ax, %ds

    inb $0x92, %al
    or $0x02, %al
    outb %al, $0x92

    /* 加载临时 GDT (相对于 0x8000 的偏移) */
    lgdt (ap_gdt_ptr - ap_trampoline_start)

    /* 开启保护模式 (CR0.PE = 1) */
    mov %cr0, %eax
    or $0x01, %al
    mov %eax, %cr0

    /* 远跳转到 32 位代码段, 刷新 CS */
    /* 注意: 目标地址是 AP_TRAMPOLINE_ADDR + 偏移 */
    ljmp $0x08, $(AP_TRAMPOLINE_ADDR + ap_entry_32 - ap_trampoline_start)

.code32
/**
 * AP 32 位入口点
 *
 * 此时已进入 32 位保护模式
 */
ap_entry_32:
    mov $AP_TRAMPOLINE_ADDR, %ebp

    /* 加载数据段选择子 */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    ljmp $0x08, $(AP_TRAMPOLINE_ADDR + ap_entry_kernel - ap_trampoline_start)

ap_entry_kernel:
    /* 重新加载数据段 */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $AP_TRAMPOLINE_STACK_TOP, %esp

    /* 检查并处理 x2APIC 模式 (切换到 xAPIC) */
    mov $MSR_IA32_APIC_BASE, %ecx
    rdmsr
    test $APIC_BASE_X2APIC, %eax
    jz 1f
    and $~(APIC_BASE_ENABLE | APIC_BASE_X2APIC), %eax
    wrmsr
    or $APIC_BASE_ENABLE, %eax
    wrmsr
1:
    /* 启用分页 */
    mov ap_kernel_cr3 - ap_trampoline_start(%ebp), %eax
    mov %eax, %cr3
    mov %cr0, %eax
    or $CR0_PG, %eax
    mov %eax, %cr0
    jmp 2f
2:
    lgdt ap_kernel_gdtr - ap_trampoline_start(%ebp)
    ljmp $0x08, $(AP_TRAMPOLINE_ADDR + ap_entry_paged - ap_trampoline_start)

ap_entry_paged:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    lidt ap_kernel_idtr - ap_trampoline_start(%ebp)

    /* 获取 CPU ID (通过 LAPIC ID) */
    /* LAPIC ID 寄存器在 LAPIC_BASE + LAPIC_ID_REG */
    mov $(LAPIC_BASE + LAPIC_ID_REG), %eax
    mov (%eax), %eax
    shr $24, %eax
    /* EAX = LAPIC ID */

    /* 查找对应的 CPU 栈 */
    /* 栈地址存储在 ap_stacks 数组中 */
    /* 使用 LAPIC ID 作为索引查找 */
    mov %eax, %ebx              /* 保存 LAPIC ID */

    /* 遍历 LAPIC ID 数组找到对应的 CPU 索引 */
    xor %ecx, %ecx              /* CPU 索引 = 0 */
.find_cpu_loop:
    cmp $CFG_MAX_CPUS, %ecx     /* 最多 CFG_MAX_CPUS 个 CPU */
    jge .cpu_not_found

    movzbl ap_lapic_ids - ap_trampoline_start(%ebp, %ecx, 1), %edx
    cmp %ebx, %edx
    je .cpu_found

    inc %ecx
    jmp .find_cpu_loop

.cpu_found:
    /* ECX = CPU 索引 */
    /* 加载对应的栈指针 */
    mov ap_stacks - ap_trampoline_start(%ebp, %ecx, 4), %esp

    /* 压入 CPU ID 作为参数 */
    push %ecx

    /* 调用 C 入口函数 */
    mov $ap_main, %eax
    call *%eax

    /* 不应返回 */
.cpu_not_found:
    cli
    hlt
    jmp .cpu_not_found

/* 对齐到 4 字节 */
.align 4

/**
 * 临时 GDT (用于从实模式切换到保护模式)
 */
ap_gdt:
    /* NULL 描述符 */
    .quad 0x0000000000000000

    /* 代码段: base=0, limit=4GB, 32-bit, Ring 0 */
    .quad 0x00CF9A000000FFFF

    /* 数据段: base=0, limit=4GB, 32-bit, Ring 0 */
    .quad 0x00CF92000000FFFF

ap_gdt_ptr:
    .word ap_gdt_ptr - ap_gdt - 1   /* limit */
    .long AP_TRAMPOLINE_ADDR + ap_gdt - ap_trampoline_start  /* base (物理地址) */

/**
 * 以下变量由 BSP 在启动 AP 前填充
 */

/* 内核 GDT 指针 (由 BSP 填充) */
.global ap_kernel_gdtr
ap_kernel_gdtr:
    .word 0     /* limit */
    .long 0     /* base */

.global ap_kernel_idtr
ap_kernel_idtr:
    .word 0
    .long 0

.global ap_kernel_cr3
ap_kernel_cr3:
    .long 0

/* 每个 CPU 的栈指针数组 (由 BSP 填充) */
.global ap_stacks
ap_stacks:
    .long 0, 0, 0, 0, 0, 0, 0, 0    /* 最多 8 个 CPU */

/* 每个 CPU 的 LAPIC ID 数组 (由 BSP 填充) */
.global ap_lapic_ids
ap_lapic_ids:
    .byte 0, 0, 0, 0, 0, 0, 0, 0    /* 最多 8 个 CPU */

/* Trampoline 结束标记 */
.global ap_trampoline_end
ap_trampoline_end:

/* 栈保护标记 */
.section .note.GNU-stack, "", @progbits
