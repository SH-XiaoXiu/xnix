/**
 * @file resource_dynamic.c
 * @brief 资源分配器动态实现(强符号)
 *
 * 可扩展位图,容量不足时自动扩容.
 * 编译此文件时会覆盖 resource_static.c 中的弱符号.
 */

#include <xnix/mm.h>
#include <xnix/resource.h>
#include <xnix/string.h>

int bitmap_alloc_init(struct bitmap_allocator *alloc, uint32_t initial_cap, uint32_t max_cap) {
    if (!alloc || initial_cap == 0) {
        return -1;
    }

    /* 容量对齐到 32 位 */
    uint32_t cap       = (initial_cap + 31) & ~31;
    size_t   bitmap_sz = cap / 8;

    alloc->bitmap = kzalloc(bitmap_sz);
    if (!alloc->bitmap) {
        return -1;
    }

    alloc->capacity     = cap;
    alloc->used         = 0;
    alloc->max_capacity = max_cap; /* 0 = 无限制 */

    return 0;
}

int32_t bitmap_alloc_get(struct bitmap_allocator *alloc) {
    if (!alloc || !alloc->bitmap) {
        return -1;
    }

    uint32_t word_count = alloc->capacity / 32;

    /* 查找空闲位 */
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

    /* 尝试扩容 */
    if (alloc->max_capacity == 0 || alloc->capacity < alloc->max_capacity) {
        uint32_t new_cap = alloc->capacity * 2;

        /* 受限于 max_capacity */
        if (alloc->max_capacity != 0 && new_cap > alloc->max_capacity) {
            new_cap = alloc->max_capacity;
        }

        if (new_cap <= alloc->capacity) {
            return -1; /* 无法扩容 */
        }

        size_t    new_sz     = new_cap / 8;
        uint32_t *new_bitmap = kzalloc(new_sz);
        if (!new_bitmap) {
            return -1;
        }

        memcpy(new_bitmap, alloc->bitmap, alloc->capacity / 8);
        kfree(alloc->bitmap);

        uint32_t old_cap = alloc->capacity;
        alloc->bitmap    = new_bitmap;
        alloc->capacity  = new_cap;

        /* 分配扩容后的第一个新位 */
        uint32_t new_id = old_cap;
        alloc->bitmap[new_id / 32] |= (1UL << (new_id % 32));
        alloc->used++;

        return (int32_t)new_id;
    }

    return -1; /* 已满 */
}

void bitmap_alloc_put(struct bitmap_allocator *alloc, int32_t id) {
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
