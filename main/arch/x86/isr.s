/**
 * @file isr.s
 * @brief 中断服务程序入口
 * @author XiaoXiu
 */

.section .text
.code32

/* 加载 GDT */
.global gdt_load
gdt_load:
    mov 4(%esp), %eax
    lgdt (%eax)
    /* 重新加载段寄存器 */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    /* 远跳转刷新 CS */
    ljmp $0x08, $.gdt_flush
.gdt_flush:
    ret

/* 加载 IDT */
.global idt_load
idt_load:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

/* 无错误码的异常入口宏 */
.macro ISR_NOERR num
.global isr\num
isr\num:
    push $0             /* 假错误码 */
    push $\num          /* 中断号 */
    jmp isr_common
.endm

/* 有错误码的异常入口宏 */
.macro ISR_ERR num
.global isr\num
isr\num:
    push $\num          /* 中断号 (错误码已由 CPU 压栈) */
    jmp isr_common
.endm

/* IRQ 入口宏 */
.macro IRQ num, int_num
.global irq\num
irq\num:
    push $0             /* 假错误码 */
    push $\int_num      /* 中断号 */
    jmp irq_common
.endm

/* CPU 异常 0-31 */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

/* IRQ 0-15 -> 中断 32-47 */
IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

/* 异常通用处理 */
isr_common:
    pusha               /* 保存通用寄存器 */
    push %ds

    mov $0x10, %ax      /* 内核数据段 */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp           /* 传递栈帧指针 */
    call isr_handler
    add $4, %esp

    pop %ds
    popa
    add $8, %esp        /* 跳过中断号和错误码 */
    iret

/* IRQ 通用处理 */
irq_common:
    pusha
    push %ds

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp
    call irq_handler
    add $4, %esp

    pop %ds
    popa
    add $8, %esp
    iret

/* 系统调用入口 (int 0x80) */
.global isr_syscall
isr_syscall:
    push $0             /* 假错误码 */
    push $0x80          /* 中断号 */
    
    pusha               /* 保存通用寄存器 */
    push %ds

    mov $0x10, %ax      /* 内核数据段 */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp           /* 传递栈帧指针 */
    call syscall_handler
    add $4, %esp

    pop %ds
    popa
    add $8, %esp        /* 跳过中断号和错误码 */
    iret
