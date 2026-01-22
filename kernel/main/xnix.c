/**
 * @file xnix.c
 * @brief Xnix 内核入口
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <xstd/stdio.h>
#include <arch/console.h>
#include <arch/cpu.h>

void kernel_main(void) {
    arch_console_init();

    kprintf("\n");
    kprintf("========================================\n");
    kprintf("        Xnix Kernel Loaded!\n");
    kprintf("========================================\n");
    kprintf("\n");
    kprintf("Console initialized.\n");
    kprintf("Test: int=%d, hex=0x%x, str=%s\n", 42, 0xDEAD, "hello");

    while (1) {
        arch_halt();
    }
}
