/**
 * @file tss.c
 * @brief TSS 管理实现
 * @author XiaoXiu
 * @see https://ysos.gzti.me/
 *
 * 支持 Per-CPU TSS 用于 SMP
 */

#include <arch/smp.h>
#include <arch/x86/tss.h>

#include <xnix/config.h>
#include <xnix/string.h>

/* Per-CPU TSS 数组 */
static struct tss_entry tss[CFG_MAX_CPUS];

/**
 * 初始化所有 TSS (BSP 调用)
 */
void tss_init(void) {
    for (uint32_t i = 0; i < CFG_MAX_CPUS; i++) {
        memset(&tss[i], 0, sizeof(struct tss_entry));
        tss[i].ss0  = 0x10; /* KERNEL_DS */
        tss[i].esp0 = 0;    /* 初始为 0, 调度时会更新 */
        /* I/O Map Base = sizeof(tss) 表示没有 I/O Bitmap */
        tss[i].iomap_base = sizeof(struct tss_entry);
    }
}

/**
 * 初始化指定 CPU 的 TSS (AP 调用)
 */
void tss_init_cpu(uint32_t cpu_id) {
    if (cpu_id >= CFG_MAX_CPUS) {
        return;
    }
    memset(&tss[cpu_id], 0, sizeof(struct tss_entry));
    tss[cpu_id].ss0        = 0x10; /* KERNEL_DS */
    tss[cpu_id].esp0       = 0;
    tss[cpu_id].iomap_base = sizeof(struct tss_entry);
}

/**
 * 设置当前 CPU 的 TSS 栈指针
 */
void tss_set_stack(uint32_t ss0, uint32_t esp0) {
    uint32_t cpu = cpu_current_id();
    if (cpu >= CFG_MAX_CPUS) {
        cpu = 0;
    }
    tss[cpu].ss0  = ss0;
    tss[cpu].esp0 = esp0;
}

/**
 * 设置指定 CPU 的 TSS 栈指针
 */
void tss_set_stack_cpu(uint32_t cpu_id, uint32_t ss0, uint32_t esp0) {
    if (cpu_id >= CFG_MAX_CPUS) {
        return;
    }
    tss[cpu_id].ss0  = ss0;
    tss[cpu_id].esp0 = esp0;
}

/**
 * 获取指定 CPU 的 TSS 描述符信息
 */
void tss_get_desc(uint32_t cpu_id, uint32_t *base, uint32_t *limit) {
    if (cpu_id >= CFG_MAX_CPUS) {
        cpu_id = 0;
    }
    *base  = (uint32_t)&tss[cpu_id];
    *limit = sizeof(struct tss_entry) - 1;
}
