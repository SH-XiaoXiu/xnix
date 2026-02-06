#include <ipc/msg_pool.h>
#include <xnix/config.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/sync.h>

#if CFG_IPC_MSG_POOL

/*
 * 全局 IPC kmsg 池实现
 *
 * 数据结构:
 * - free-list:单向链表,节点为 struct ipc_kmsg,受 g_kmsg_pool_lock 保护
 *
 * 并发与时序:
 * - free-list 操作必须持有 g_kmsg_pool_lock(并关中断)以避免与中断/其他 CPU 并发
 * - 节点 refcount 使用原子操作,允许在不持锁的情况下增减引用计数
 *
 * 生命周期:
 * - 节点通过 kmalloc 分配成块(chunk),随后被挂入 free-list
 * - 节点不会单独 kfree;回收只是回到 free-list(适合内核常驻池)
 */

#define IPC_KMSG_GROW_CHUNK 128u

static spinlock_t       g_kmsg_pool_lock = SPINLOCK_INIT;
static struct ipc_kmsg *g_kmsg_free_list = NULL;
static uint32_t         g_kmsg_total     = 0;
static uint32_t         g_kmsg_free      = 0;

/**
 * @brief 扩容消息池(将 count 个新节点加入 free-list)
 *
 * @return true 成功,false 失败
 *
 * 实现说明:
 * - 采用"块分配 + 切片"为多个节点,减少频繁小额 kmalloc.
 * - 分配成功后一次性持锁把所有节点挂入 free-list,避免长时间反复加解锁.
 * - 统计计数仅用于诊断,不参与功能逻辑.
 */
static bool ipc_kmsg_pool_grow(uint32_t count) {
    size_t           size  = (size_t)count * sizeof(struct ipc_kmsg);
    struct ipc_kmsg *block = kmalloc(size);
    if (!block) {
        return false;
    }

    uint32_t flags = spin_lock_irqsave(&g_kmsg_pool_lock);
    for (uint32_t i = 0; i < count; i++) {
        struct ipc_kmsg *m = &block[i];
        atomic_set(&m->refcount, 0);
        m->next          = NULL;
        m->next          = g_kmsg_free_list;
        g_kmsg_free_list = m;
        g_kmsg_total++;
        g_kmsg_free++;
    }
    spin_unlock_irqrestore(&g_kmsg_pool_lock, flags);

    return true;
}

/**
 * @brief 初始化全局消息池
 *
 * 约定:
 * - 由 ipc_init() 调用一次即可.
 * - 初始化失败时打印告警,后续 ipc_kmsg_alloc() 将可能返回 NULL.
 */
void ipc_kmsg_pool_init(void) {
    if (!ipc_kmsg_pool_grow(IPC_KMSG_GROW_CHUNK)) {
        pr_warn("IPC: kmsg pool init failed");
        return;
    }
    pr_info("IPC: kmsg pool enabled");
}

/**
 * @brief 分配一个消息节点(refcount=1)
 *
 * @return 成功返回节点指针,失败返回 NULL
 *
 * 行为:
 * - 优先从 free-list 弹出;若为空则尝试扩容一次后再试.
 * - 成功返回时 next 被清空,调用方需要自行写入 regs.
 */
struct ipc_kmsg *ipc_kmsg_alloc(void) {
    for (int attempt = 0; attempt < 2; attempt++) {
        uint32_t         flags = spin_lock_irqsave(&g_kmsg_pool_lock);
        struct ipc_kmsg *m     = g_kmsg_free_list;
        if (m) {
            g_kmsg_free_list = m->next;
            g_kmsg_free--;
            spin_unlock_irqrestore(&g_kmsg_pool_lock, flags);

            m->next = NULL;
            atomic_set(&m->refcount, 1);
            return m;
        }
        spin_unlock_irqrestore(&g_kmsg_pool_lock, flags);

        if (!ipc_kmsg_pool_grow(IPC_KMSG_GROW_CHUNK)) {
            break;
        }
    }

    return NULL;
}

/**
 * @brief 增加引用计数
 *
 * 用于同一节点被多处持有的场景(当前 endpoint 异步队列一般不需要额外 get).
 */
void ipc_kmsg_get(struct ipc_kmsg *m) {
    if (!m) {
        return;
    }
    atomic_inc(&m->refcount);
}

/**
 * @brief 减少引用计数并在为 0 时回收到 free-list
 *
 * 说明:
 * - 当 refcount 递减后不为 0,说明仍有持有者,直接返回.
 * - 当 refcount 递减后为 0,将节点挂回 free-list 以便复用.
 */
void ipc_kmsg_put(struct ipc_kmsg *m) {
    if (!m) {
        return;
    }
    if (atomic_dec(&m->refcount) != 0) {
        return;
    }

    uint32_t flags   = spin_lock_irqsave(&g_kmsg_pool_lock);
    m->next          = g_kmsg_free_list;
    g_kmsg_free_list = m;
    g_kmsg_free++;
    spin_unlock_irqrestore(&g_kmsg_pool_lock, flags);
}

#endif
