// Copyright (c) 2026 XiaoXiu
// SPDX-License-Identifier: MIT

/**
 * @file mm.c
 * @brief 内存管理初始化入口
 */

#include <arch/hal/feature.h>

#include <kernel/process/process.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/usraccess.h>
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
        arch_register_mm_ops();
    } else {
        /* No-MMU 模式 */
        mm_register_ops(mm_get_nommu_ops());
    }

    if (g_mm_ops && g_mm_ops->init) {
        g_mm_ops->init();
    }
}

void mm_dump_stats(void) {
    uint32_t total = page_alloc_total_count();
    uint32_t free  = page_alloc_free_count();
    uint32_t used  = total - free;

    pr_info("Memory: total %u KB, used %u KB, free %u KB (%u/%u pages)", total * 4, used * 4,
            free * 4, used, total);
}

extern void *vmm_kmap(paddr_t paddr);
extern void  vmm_kunmap(void *vaddr);

/*
 * 用户态内存访问(最小实现)
 *
 * 思路:
 * - 不直接解引用 user 指针(避免内核页故障/越权)
 * - 通过 mm_ops->query 将 user vaddr 解析为物理地址
 * - 逐页用 vmm_kmap 临时映射到内核,再 memcpy
 *
 * 限制:
 * - 目前不检查 PTE 的 user/write 权限,仅判断是否映射
 * - vmm_kmap 依赖临时窗口,持锁期间不能睡眠
 */
int copy_from_user(void *dst, const void *user_src, size_t n) {
    if (!dst || (!user_src && n)) {
        return -EINVAL;
    }
    if (!n) {
        return 0;
    }
    if (!hal_has_feature(HAL_FEATURE_MMU)) {
        return -ENOSYS;
    }

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        return -ENOSYS;
    }

    /* 获取当前进程的页目录,不依赖 CR3 寄存器 */
    struct process *cur = process_get_current();
    void           *pd  = (cur && cur->page_dir_phys) ? cur->page_dir_phys : NULL;

    size_t copied = 0;
    while (copied < n) {
        uintptr_t uaddr = (uintptr_t)user_src + copied;
        uintptr_t paddr = mm->query(pd, uaddr);
        if (!paddr) {
            return -EFAULT;
        }

        size_t page_off   = (size_t)(uaddr & (PAGE_SIZE - 1));
        size_t chunk_size = PAGE_SIZE - page_off;
        if (copied + chunk_size > n) {
            chunk_size = n - copied;
        }

        void *page = vmm_kmap((paddr_t)(paddr & PAGE_MASK));
        memcpy((uint8_t *)dst + copied, (uint8_t *)page + page_off, chunk_size);

        vmm_kunmap(page);

        copied += chunk_size;
    }

    return 0;
}

int copy_to_user(void *user_dst, const void *src, size_t n) {
    if ((!user_dst && n) || !src) {
        return -EINVAL;
    }
    if (!n) {
        return 0;
    }
    if (!hal_has_feature(HAL_FEATURE_MMU)) {
        return -ENOSYS;
    }

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        return -ENOSYS;
    }

    /* 获取当前进程的页目录,不依赖 CR3 寄存器 */
    struct process *cur = process_get_current();
    void           *pd  = (cur && cur->page_dir_phys) ? cur->page_dir_phys : NULL;

    size_t copied = 0;
    while (copied < n) {
        uintptr_t uaddr = (uintptr_t)user_dst + copied;
        uintptr_t paddr = mm->query(pd, uaddr);
        if (!paddr) {
            return -EFAULT;
        }

        size_t page_off   = (size_t)(uaddr & (PAGE_SIZE - 1));
        size_t chunk_size = PAGE_SIZE - page_off;
        if (copied + chunk_size > n) {
            chunk_size = n - copied;
        }

        void *page = vmm_kmap((paddr_t)(paddr & PAGE_MASK));
        memcpy((uint8_t *)page + page_off, (const uint8_t *)src + copied, chunk_size);
        vmm_kunmap(page);

        copied += chunk_size;
    }

    return 0;
}
