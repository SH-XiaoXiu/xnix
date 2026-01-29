/**
 * @file tid.c
 * @brief TID 资源管理实现
 */

#include <kernel/sched/sched.h>
#include <kernel/sched/tid.h>
#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/string.h>
#include <xnix/sync.h>

/* TID Bitmap */
static uint32_t  *tid_bitmap   = NULL;
static uint32_t   tid_capacity = 0;
static spinlock_t tid_lock     = SPINLOCK_INIT;

void tid_init(void) {
    /* 分配 TID Bitmap (初始容量使用配置值) */
    tid_capacity       = (CFG_INITIAL_THREADS + 31) & ~31; /* 32 对齐 */
    size_t bitmap_size = (tid_capacity / 8);
    tid_bitmap         = kzalloc(bitmap_size);
    if (!tid_bitmap) {
        panic("Failed to allocate TID bitmap");
    }

    /* 初始化 TID Bitmap (保留 TID 0) */
    tid_bitmap[0] |= 1;
}

void tid_free(tid_t tid) {
    if (tid == 0 || tid >= (int32_t)tid_capacity) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&tid_lock);
    tid_bitmap[tid / 32] &= ~(1UL << (tid % 32));
    spin_unlock_irqrestore(&tid_lock, flags);
}

tid_t tid_alloc(void) {
    uint32_t flags = spin_lock_irqsave(&tid_lock);

    for (uint32_t i = 0; i < (tid_capacity + 31) / 32; i++) {
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
                return tid;
            }
        }
    }

    /* 扩容 */
    uint32_t  new_capacity = tid_capacity * 2;
    uint32_t *new_bitmap   = kzalloc((new_capacity + 31) / 8);
    if (!new_bitmap) {
        spin_unlock_irqrestore(&tid_lock, flags);
        return TID_INVALID;
    }

    memcpy(new_bitmap, tid_bitmap, (tid_capacity + 31) / 8);
    kfree(tid_bitmap);
    tid_bitmap   = new_bitmap;
    tid_capacity = new_capacity;

    /* 返回新扩容部分的第一个 ID */
    /* 上一次循环结束时, tid_capacity 之前的位都已满(或在循环中被跳过),
       但为了简单和正确,我们可以直接分配 tid_capacity / 2 (即旧容量的第一个新位) */
    /* 注意: tid_capacity 肯定是 32 的倍数(初始化时保证),所以 old_cap 就是新分配区域的起始 bit */
    tid_t tid = tid_capacity / 2;
    tid_bitmap[tid / 32] |= (1UL << (tid % 32));

    spin_unlock_irqrestore(&tid_lock, flags);
    return tid;
}
