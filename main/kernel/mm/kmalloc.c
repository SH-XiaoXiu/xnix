// Copyright (c) 2026 XiaoXiu
// SPDX-License-Identifier: MIT

/**
 * @file kmalloc.c
 * @brief 内核堆分配器(简单版)
 *
 * 直接包装页分配器,任何分配都向上取整到整数页
 * kmalloc(1) 实际给 4096 字节,浪费但简单
 * 后续可加 Slab 分配器优化小对象
 * alloc_pages返回 kmalloc_header (8 bytes)  kmalloc返回用户数据区给用户
 */

#include <arch/mmu.h>

#include <xnix/mm.h>
#include <xnix/string.h>

/* header 8 字节,保证返回地址 8 字节对齐 */
struct kmalloc_header {
    uint32_t pages;    /* 分配的页数 */
    uint32_t reserved; /* 保留,可用于 magic/调试 */
};

_Static_assert(sizeof(struct kmalloc_header) == 8, "header must be 8 bytes");

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    /* 总大小 = header + 用户请求 */
    size_t   total = sizeof(struct kmalloc_header) + size;
    uint32_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;

    void *ptr = alloc_pages(pages);
    if (!ptr) {
        return NULL;
    }

    /* 记录页数 */
    struct kmalloc_header *hdr = ptr;
    hdr->pages                 = pages;
    hdr->reserved              = 0;

    return (char *)ptr + sizeof(struct kmalloc_header);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) {
        return;
    }

    /* 找到 header */
    struct kmalloc_header *hdr =
        (struct kmalloc_header *)((char *)ptr - sizeof(struct kmalloc_header));

    free_pages(hdr, hdr->pages);
}
