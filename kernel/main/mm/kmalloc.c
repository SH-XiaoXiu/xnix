/**
 * @file kmalloc.c
 * @brief 内核堆分配器（简单版）
 *
 * 直接包装页分配器，任何分配都向上取整到整数页
 * kmalloc(1) 实际给 4096 字节，浪费但简单
 * 后续可加 Slab 分配器优化小对象
 */

#include <arch/mmu.h>

#include <xnix/mm.h>
#include <xnix/string.h>

void *kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    return alloc_pages(pages);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        memset(ptr, 0, pages * PAGE_SIZE);
    }
    return ptr;
}

/*
 * kfree 的问题：不知道这块内存多大
 * 临时方案：只释放一页，多页分配会泄漏
 * TODO: 在分配时记录大小，或用 Slab
 */
void kfree(void *ptr) {
    if (!ptr) {
        return;
    }
    free_page(ptr);
}
