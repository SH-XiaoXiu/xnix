/**
 * @file tss.c
 * @brief TSS 管理实现
 * @author XiaoXiu
 * @see https://ysos.gzti.me/
 *
 * 支持 Per-CPU TSS 用于 SMP
 */

#include <arch/smp.h>

#include <asm/tss.h>
#include <xnix/config.h>
#include <xnix/percpu.h>
#include <xnix/string.h>

/* Per-CPU TSS */
static DEFINE_PER_CPU(struct tss_entry, tss);

/**
 * 初始化所有 TSS (BSP 调用)
 */
void tss_init(void) {
    for (uint32_t i = 0; i < CFG_MAX_CPUS; i++) {
        struct tss_entry *t = per_cpu_ptr(tss, i);
        memset(t, 0, sizeof(struct tss_entry));
        t->ss0  = 0x10; /* KERNEL_DS */
        t->esp0 = 0;    /* 初始为 0, 调度时会更新 */
        /* I/O Map Base = sizeof(tss) 表示没有 I/O Bitmap */
        t->iomap_base = sizeof(struct tss_entry);
    }
}

/**
 * 初始化指定 CPU 的 TSS (AP 调用)
 */
void tss_init_cpu(uint32_t cpu_id) {
    if (cpu_id >= CFG_MAX_CPUS) {
        return;
    }
    struct tss_entry *t = per_cpu_ptr(tss, cpu_id);
    memset(t, 0, sizeof(struct tss_entry));
    t->ss0        = 0x10; /* KERNEL_DS */
    t->esp0       = 0;
    t->iomap_base = sizeof(struct tss_entry);
}

/**
 * 设置当前 CPU 的 TSS 栈指针
 */
void tss_set_stack(uint32_t ss0, uint32_t esp0) {
    struct tss_entry *t = this_cpu_ptr(tss);
    t->ss0              = ss0;
    t->esp0             = esp0;
}

/**
 * 设置指定 CPU 的 TSS 栈指针
 */
void tss_set_stack_cpu(uint32_t cpu_id, uint32_t ss0, uint32_t esp0) {
    if (cpu_id >= CFG_MAX_CPUS) {
        return;
    }
    struct tss_entry *t = per_cpu_ptr(tss, cpu_id);
    t->ss0              = ss0;
    t->esp0             = esp0;
}

/**
 * 获取指定 CPU 的 TSS 描述符信息
 */
void tss_get_desc(uint32_t cpu_id, uint32_t *base, uint32_t *limit) {
    if (cpu_id >= CFG_MAX_CPUS) {
        cpu_id = 0;
    }
    *base  = (uint32_t)per_cpu_ptr(tss, cpu_id);
    *limit = sizeof(struct tss_entry) - 1;
}
