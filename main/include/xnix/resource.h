/**
 * @file resource.h
 * @brief 资源分配器抽象接口
 *
 * 支持静态/动态两种分配模式,通过编译裁切选择.
 */

#ifndef XNIX_RESOURCE_H
#define XNIX_RESOURCE_H

#include <xnix/types.h>

/**
 * 位图分配器(用于 TID/PID 等整数 ID 分配)
 */
struct bitmap_allocator {
    uint32_t *bitmap;       /* 位图数据 */
    uint32_t  capacity;     /* 当前容量(位数) */
    uint32_t  used;         /* 已分配数量 */
    uint32_t  max_capacity; /* 最大容量(0 = 无限制,可动态扩展) */
};

/**
 * 初始化位图分配器
 *
 * @param alloc        分配器指针
 * @param initial_cap  初始容量
 * @param max_cap      最大容量(0 = 动态扩展无限制)
 * @return 0 成功,负数失败
 */
int bitmap_alloc_init(struct bitmap_allocator *alloc, uint32_t initial_cap, uint32_t max_cap);

/**
 * 分配一个 ID
 *
 * @param alloc 分配器指针
 * @return 分配的 ID,失败返回 -1
 */
int32_t bitmap_alloc_get(struct bitmap_allocator *alloc);

/**
 * 释放一个 ID
 *
 * @param alloc 分配器指针
 * @param id    要释放的 ID
 */
void bitmap_alloc_put(struct bitmap_allocator *alloc, int32_t id);

/**
 * 检查 ID 是否有效(已分配)
 *
 * @param alloc 分配器指针
 * @param id    要检查的 ID
 * @return true 如果已分配
 */
bool bitmap_alloc_is_used(struct bitmap_allocator *alloc, int32_t id);

/**
 * 获取分配器统计信息
 */
uint32_t bitmap_alloc_capacity(struct bitmap_allocator *alloc);
uint32_t bitmap_alloc_used(struct bitmap_allocator *alloc);

/**
 * 销毁分配器
 */
void bitmap_alloc_destroy(struct bitmap_allocator *alloc);

#endif /* XNIX_RESOURCE_H */
