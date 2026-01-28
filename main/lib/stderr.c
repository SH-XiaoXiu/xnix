/**
 * @file stderr.c
 * @brief 内核错误处理机制 (Panic/Assert)
 * @author XiaoXiu
 * @date 2026-01-25
 */

#include <xnix/console.h>

#include <asm/cpu.h>
#include <xnix/debug.h>
#include <xnix/stdio.h>

static void dump_stack(void) {
    uint32_t *ebp, *eip;

    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));

    kputs("\nStack Trace:\n");

    int depth = 0;
    while (ebp && depth < 16) {
        eip = (uint32_t *)*(ebp + 1);
        kprintf("  [%d] EIP: 0x%08x  EBP: 0x%08x\n", depth++, eip, ebp);

        /* 获取上一个栈帧的 EBP */
        uint32_t *next_ebp = (uint32_t *)*ebp;

        /* 简单的有效性检查,防止死循环或访问非法内存 */
        if (next_ebp <= ebp || (uint32_t)next_ebp < 0x100000) {
            break;
        }
        ebp = next_ebp;
    }
}

void panic(const char *fmt, ...) {
    /* 立即关中断,防止干扰 */
    cpu_irq_disable();

    /* 进入紧急模式,直接输出到串口 */
    console_emergency_mode();

    /* 红色分割线 */
    console_set_color(KCOLOR_RED);
    kputs("\n\n");
    kputs("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    kputs("!!               KERNEL PANIC                  !!\n");
    kputs("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    kputs("Reason: ");

    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vkprintf(fmt, args);
    __builtin_va_end(args);

    /* 打印调用栈 */
    dump_stack();

    kputs("\nSystem Halted.\n");

    /* 死循环挂起 */
    while (1) {
        cpu_halt();
    }
}

void __assert_fail(const char *expr, const char *file, int line, const char *func) {
    panic("Assertion failed: %s\nAt: %s:%d\nFunction: %s", expr, file, line, func);
}
