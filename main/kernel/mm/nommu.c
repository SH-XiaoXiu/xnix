#include <xnix/mm_ops.h>
#include <xnix/stdio.h>

/*
 * No-MMU (Identity Mapping) 适配器实现
 * 适用于 MCU 或为了调试目的禁用 MMU 的场景
 */

static void nommu_init(void) {
    pr_warn("No-MMU mode initialized (Identity Mapping active)");
}

static void *nommu_create_as(void) {
    /* No-MMU 模式下没有独立的地址空间,返回一个伪句柄 */
    return (void *)0xDEADBEEF;
}

static void nommu_destroy_as(void *as) {
    (void)as;
}

static void nommu_switch_as(void *as) {
    /* 不做任何事,所有进程共享同一地址空间 */
    (void)as;
}

static int nommu_map(void *as, uintptr_t vaddr, uintptr_t paddr, uint32_t flags) {
    (void)as;
    (void)flags;

    /* 检查是否请求了 1:1 映射 */
    if (vaddr != paddr) {
        pr_err("No-MMU mode requires vaddr == paddr (requested v=%p p=%p)", vaddr, paddr);
        return -1;
    }
    return 0;
}

static void nommu_unmap(void *as, uintptr_t vaddr) {
    (void)as;
    (void)vaddr;
}

static uintptr_t nommu_query(void *as, uintptr_t vaddr) {
    (void)as;
    /* 恒等映射 */
    return vaddr;
}

static const struct mm_operations nommu_ops = {
    .name       = "no-mmu",
    .init       = nommu_init,
    .create_as  = nommu_create_as,
    .destroy_as = nommu_destroy_as,
    .switch_as  = nommu_switch_as,
    .map        = nommu_map,
    .unmap      = nommu_unmap,
    .query      = nommu_query,
};

/* 获取 No-MMU 操作集 */
const struct mm_operations *mm_get_nommu_ops(void) {
    return &nommu_ops;
}
