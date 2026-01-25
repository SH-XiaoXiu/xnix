// Copyright (c) 2026 XiaoXiu
// SPDX-License-Identifier: MIT

/**
 * @file mm.c
 * @brief 内存管理初始化入口
 */

#include <xnix/mm.h>
#include <xnix/stdio.h>

/* page_alloc.c 中定义 */
extern void page_alloc_init(void);
extern uint32_t page_alloc_free_count(void);
extern uint32_t page_alloc_total_count(void);

void mm_init(void) {
    kprintf("Initializing memory manager...\n");
    page_alloc_init();
}

void mm_dump_stats(void) {
    uint32_t total = page_alloc_total_count();
    uint32_t free = page_alloc_free_count();
    uint32_t used = total - free;

    kprintf("Memory: total %u KB, used %u KB, free %u KB (%u/%u pages)\n",
            total * 4, used * 4, free * 4, used, total);
}
