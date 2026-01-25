/**
 * @file tss.c
 * @brief TSS 管理实现
 * @author XiaoXiu
 * @see https://ysos.gzti.me/
 */

#include <arch/x86/tss.h>

#include <xnix/string.h>

static struct tss_entry tss;

void tss_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.ss0  = 0x10; /* KERNEL_DS */
    tss.esp0 = 0;    /* 初始为 0, 调度时会更新 */
    /* I/O Map Base = sizeof(tss) 表示没有 I/O Bitmap */
    tss.iomap_base = sizeof(tss);
}

void tss_set_stack(uint32_t ss0, uint32_t esp0) {
    tss.ss0  = ss0;
    tss.esp0 = esp0;
}

void tss_get_desc(uint32_t *base, uint32_t *limit) {
    *base  = (uint32_t)&tss;
    *limit = sizeof(tss) - 1;
}
