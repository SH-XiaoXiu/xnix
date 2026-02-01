/**
 * @file resource_static.c
 * @brief 资源分配器静态实现(弱符号)
 *
 * 固定大小位图,不支持扩容.
 * 当编译动态版本时,强符号会覆盖这些函数.
 */

#include <xnix/mm.h>
#include <xnix/resource.h>
#include <xnix/string.h>

__attribute__((weak)) int bitmap_alloc_init(struct bitmap_allocator *alloc, uint32_t initial_cap,
                                            uint32_t max_cap) {
    if (!alloc || initial_cap == 0) {
        return -1;
    }

    /* 静态版本使用 max_cap 作为固定容量 */
    uint32_t cap = (max_cap > 0) ? max_cap : initial_cap;
    cap          = (cap + 31) & ~31;

    size_t bitmap_sz = cap / 8;
    alloc->bitmap    = kzalloc(bitmap_sz);
    if (!alloc->bitmap) {
        return -1;
    }

    alloc->capacity     = cap;
    alloc->used         = 0;
    alloc->max_capacity = cap; /* 静态版本容量固定 */

    return 0;
}

__attribute__((weak)) int32_t bitmap_alloc_get(struct bitmap_allocator *alloc) {
    if (!alloc || !alloc->bitmap) {
        return -1;
    }

    uint32_t word_count = alloc->capacity / 32;

    for (uint32_t i = 0; i < word_count; i++) {
        if (alloc->bitmap[i] == 0xFFFFFFFF) {
            continue;
        }

        for (int j = 0; j < 32; j++) {
            uint32_t id = i * 32 + j;
            if (id >= alloc->capacity) {
                break;
            }

            if (!((alloc->bitmap[i] >> j) & 1)) {
                alloc->bitmap[i] |= (1UL << j);
                alloc->used++;
                return (int32_t)id;
            }
        }
    }

    return -1; /* 静态版本不扩容 */
}

__attribute__((weak)) void bitmap_alloc_put(struct bitmap_allocator *alloc, int32_t id) {
    if (!alloc || !alloc->bitmap || id < 0 || (uint32_t)id >= alloc->capacity) {
        return;
    }

    uint32_t idx = (uint32_t)id;
    if ((alloc->bitmap[idx / 32] >> (idx % 32)) & 1) {
        alloc->bitmap[idx / 32] &= ~(1UL << (idx % 32));
        if (alloc->used > 0) {
            alloc->used--;
        }
    }
}
