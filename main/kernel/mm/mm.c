// Copyright (c) 2026 XiaoXiu
// SPDX-License-Identifier: MIT

/**
 * @file mm.c
 * @brief 内存管理初始化入口
 */

#include <arch/hal/feature.h>

#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h>
#include <xnix/vmm.h>

/* page_alloc.c 中定义 */
extern void     page_alloc_init(void);
extern uint32_t page_alloc_free_count(void);
extern uint32_t page_alloc_total_count(void);

/* 外部声明 */
extern const struct mm_operations *mm_get_nommu_ops(void);
extern void                        arch_register_mm_ops(void); /* Arch 实现 */

/* 全局 ops 指针 */
static const struct mm_operations *g_mm_ops = NULL;

const struct mm_operations *mm_get_ops(void) {
    return g_mm_ops;
}

void mm_register_ops(const struct mm_operations *ops) {
    g_mm_ops = ops;
    pr_info("MM: Registered '%s' operations", ops->name);
}

void mm_init(void) {
    page_alloc_init();

    /* 根据 HAL 特性探测结果决定加载哪个 MM 子系统 */
    if (hal_has_feature(HAL_FEATURE_MMU)) {
        /* VMM 模式 */
        vmm_init();             /* 保留原有初始化 */
        arch_register_mm_ops(); /* 注册适配器 */
    } else {
        /* No-MMU 模式 */
        mm_register_ops(mm_get_nommu_ops());
        if (g_mm_ops && g_mm_ops->init) {
            g_mm_ops->init();
        }
    }
}

void mm_dump_stats(void) {
    uint32_t total = page_alloc_total_count();
    uint32_t free  = page_alloc_free_count();
    uint32_t used  = total - free;

    pr_info("Memory: total %u KB, used %u KB, free %u KB (%u/%u pages)", total * 4, used * 4,
            free * 4, used, total);
}
