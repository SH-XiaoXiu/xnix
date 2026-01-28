/**
 * @file asm/cpu.h
 * @brief x86 CPU 操作实现
 * @author XiaoXiu
 */

#ifndef X86_ASM_CPU_H
#define X86_ASM_CPU_H

#include <xnix/types.h>

/*
 * 中断控制
 */

static inline void cpu_irq_enable(void) {
    __asm__ volatile("sti" ::: "memory");
}

static inline void cpu_irq_disable(void) {
    __asm__ volatile("cli" ::: "memory");
}

static inline uint32_t cpu_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags)::"memory");
    return flags;
}

static inline void cpu_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0; popf" ::"r"(flags) : "memory");
}

/*
 * CPU 控制
 */

static inline void cpu_halt(void) {
    __asm__ volatile("hlt");
}

static inline void cpu_pause(void) {
    /* rep; nop 在支持 PAUSE 的 CPU 上等同于 PAUSE,在老 CPU 上等同于 NOP */
    __asm__ volatile("rep; nop");
}

static inline void cpu_stop(void) {
    __asm__ volatile("cli; hlt");
}

/*
 * I/O 端口操作
 */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

/*
 * 内存屏障
 */

static inline void barrier(void) {
    __asm__ volatile("" ::: "memory");
}

#endif
