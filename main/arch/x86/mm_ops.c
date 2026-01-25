#include <arch/mmu.h>

#include <xnix/mm_ops.h>
#include <xnix/stdio.h>
#include <xnix/vmm.h>

/*
 * x86 VMM 适配器实现
 * 将通用的 mm_ops 映射到现有的 vmm.c 实现
 */

static void x86_vmm_init(void) {
    vmm_init();
    pr_info("x86 VMM ops initialized");
}

static void *x86_vmm_create_as(void) {
    /* 创建新的页目录 */
    return (void *)vmm_create_pd();
}

static void x86_vmm_destroy_as(void *as) {
    vmm_destroy_pd(as);
}

static void x86_vmm_switch_as(void *as) {
    vmm_switch_pd(as);
}

static int x86_vmm_map(void *as, uintptr_t vaddr, uintptr_t paddr, uint32_t flags) {
    /*
     * 如果 as 为 NULL,意味着操作当前/内核空间
     * vmm_map_page 支持传递 pd_phys,如果为 NULL 则操作当前 PD (在 vmm_map_page 内部实现)
     */
    return vmm_map_page(as, vaddr, paddr, flags);
}

static void x86_vmm_unmap(void *as, uintptr_t vaddr) {
    vmm_unmap_page(as, vaddr);
}

static uintptr_t x86_vmm_query(void *as, uintptr_t vaddr) {
    return (uintptr_t)vmm_get_paddr(as, vaddr);
}

static const struct mm_operations x86_vmm_ops = {
    .name       = "x86_vmm",
    .init       = x86_vmm_init,
    .create_as  = x86_vmm_create_as,
    .destroy_as = x86_vmm_destroy_as,
    .switch_as  = x86_vmm_switch_as,
    .map        = x86_vmm_map,
    .unmap      = x86_vmm_unmap,
    .query      = x86_vmm_query,
};

/* 注册函数 */
void arch_register_mm_ops(void) {
    mm_register_ops(&x86_vmm_ops);
}
