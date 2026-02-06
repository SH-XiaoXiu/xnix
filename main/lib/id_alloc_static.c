/**
 * @file id_alloc_static.c
 * @brief 静态 ID 分配器(弱符号默认实现)
 *
 * 当 CFG_TID_DYNAMIC=OFF 时,CMake 会编译此文件.
 * 不支持动态扩容,但内存占用固定且可预测.
 *
 * 当 CFG_TID_DYNAMIC=ON 时,kernel/sched/tid.c 中的强符号会覆盖这些实现.
 */

#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/sync.h>
#include <xnix/tid.h>

/*
 * 静态 TID 分配器
 *
 * 使用固定大小的位图,容量由 CFG_MAX_THREADS 决定.
 * TID 0 保留,不分配.
 */

static uint32_t  *tid_bitmap   = NULL;
static uint32_t   tid_capacity = 0;
static spinlock_t tid_lock     = SPINLOCK_INIT;

__attribute__((weak)) void tid_init(void) {
    /* 使用固定容量(32 对齐) */
    tid_capacity = (CFG_MAX_THREADS + 31) & ~31;

    size_t bitmap_size = tid_capacity / 8;
    tid_bitmap         = kzalloc(bitmap_size);
    if (!tid_bitmap) {
        panic("Failed to allocate TID bitmap");
    }

    /* 保留 TID 0 */
    tid_bitmap[0] |= 1;
}

__attribute__((weak)) void tid_free(tid_t tid) {
    if (tid <= 0 || tid >= (int32_t)tid_capacity) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&tid_lock);
    tid_bitmap[tid / 32] &= ~(1UL << (tid % 32));
    spin_unlock_irqrestore(&tid_lock, flags);
}

__attribute__((weak)) tid_t tid_alloc(void) {
    uint32_t flags = spin_lock_irqsave(&tid_lock);

    uint32_t word_count = tid_capacity / 32;
    for (uint32_t i = 0; i < word_count; i++) {
        if (tid_bitmap[i] == 0xFFFFFFFF) {
            continue;
        }

        for (int j = 0; j < 32; j++) {
            uint32_t tid = i * 32 + j;
            if (tid >= tid_capacity) {
                break;
            }

            if (!((tid_bitmap[i] >> j) & 1)) {
                tid_bitmap[i] |= (1UL << j);
                spin_unlock_irqrestore(&tid_lock, flags);
                return (tid_t)tid;
            }
        }
    }

    /* 静态分配器不扩容,直接返回失败 */
    spin_unlock_irqrestore(&tid_lock, flags);
    return TID_INVALID;
}
