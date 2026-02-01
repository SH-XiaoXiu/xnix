/**
 * @file resource.c
 * @brief 资源分配器公共函数
 *
 * 供 resource_static.c 和 resource_dynamic.c 共用的函数.
 * init/get/put 函数由静态或动态版本提供.
 */

#include <xnix/mm.h>
#include <xnix/resource.h>

bool bitmap_alloc_is_used(struct bitmap_allocator *alloc, int32_t id) {
    if (!alloc || !alloc->bitmap || id < 0 || (uint32_t)id >= alloc->capacity) {
        return false;
    }

    uint32_t idx = (uint32_t)id;
    return (alloc->bitmap[idx / 32] >> (idx % 32)) & 1;
}

uint32_t bitmap_alloc_capacity(struct bitmap_allocator *alloc) {
    return alloc ? alloc->capacity : 0;
}

uint32_t bitmap_alloc_used(struct bitmap_allocator *alloc) {
    return alloc ? alloc->used : 0;
}

void bitmap_alloc_destroy(struct bitmap_allocator *alloc) {
    if (alloc && alloc->bitmap) {
        kfree(alloc->bitmap);
        alloc->bitmap   = NULL;
        alloc->capacity = 0;
        alloc->used     = 0;
    }
}
