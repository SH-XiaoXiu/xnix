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

#include <asm/mmu.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
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

    size_t   total = sizeof(struct kmalloc_header) + size;
    uint32_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;

    /* alloc_pages 返回物理地址 */
    paddr_t phys = (paddr_t)alloc_pages(pages);
    if (!phys) {
        return NULL;
    }

    /* 转换为虚拟地址 */
    void *virt = PHYS_TO_VIRT(phys);

    /* 记录页数 */
    struct kmalloc_header *hdr = virt;
    hdr->pages                 = pages;
    hdr->reserved              = 0;

    void *ptr = (char *)virt + sizeof(struct kmalloc_header);
    pr_debug("[MM] kmalloc: size=%zu pages=%d -> %p\n", size, pages, ptr);
    return ptr;
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

    struct kmalloc_header *hdr =
        (struct kmalloc_header *)((char *)ptr - sizeof(struct kmalloc_header));

    /* 转换虚拟地址为物理地址 */
    paddr_t phys = VIRT_TO_PHYS((uint32_t)hdr);
    pr_debug("[MM] kfree: %p pages=%d\n", ptr, hdr->pages);
    free_pages((void *)phys, hdr->pages);
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }

    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    /* 找到 header */
    struct kmalloc_header *hdr =
        (struct kmalloc_header *)((char *)ptr - sizeof(struct kmalloc_header));

    size_t old_size = hdr->pages * PAGE_SIZE - sizeof(struct kmalloc_header);

    if (new_size <= old_size) {
        /* 原地收缩(或者不作为) */
        return ptr;
    }

    /* 分配新内存 */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }

    /* 拷贝数据 */
    memcpy(new_ptr, ptr, old_size);
    kfree(ptr);

    return new_ptr;
}

void *kstrdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    void  *ptr = kmalloc(len);
    if (ptr) {
        memcpy(ptr, s, len);
    }
    return ptr;
}
