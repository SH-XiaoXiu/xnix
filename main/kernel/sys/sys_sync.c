/**
 * @file kernel/sys/sys_sync.c
 * @brief 同步原语系统调用实现(互斥锁)
 *
 * 实现用户态线程的同步原语支持,当前包括互斥锁(mutex).
 * - 锁对象存储在内核,用户态通过 handle(索引)访问
 * - 每个进程维护一个 sync_table,槽位数量由配置决定
 * - 锁基于内核已有的 mutex 实现,自动支持阻塞等待和公平调度
 * - 进程退出时自动清理所有锁资源
 */

#include <sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/process_def.h>
#include <xnix/sync.h>
#include <xnix/syscall.h>

/**
 * 在同步表中分配锁槽位
 *
 * @param table 同步对象表
 * @param m     互斥锁指针
 * @return 槽位索引(handle),失败返回负错误码
 */
static int sync_table_alloc_mutex(struct sync_table *table, mutex_t *m) {
    uint32_t flags = spin_lock_irqsave(&table->lock);

    /* 查找空闲槽位 */
    for (uint32_t i = 0; i < CFG_PROCESS_MUTEX_SLOTS; i++) {
        uint32_t bit = 1u << i;
        if (table->mutex_bitmap & bit) {
            continue; /* 槽位已占用 */
        }
        table->mutex_bitmap |= bit;
        table->mutexes[i] = m;
        spin_unlock_irqrestore(&table->lock, flags);
        return (int)i;
    }

    spin_unlock_irqrestore(&table->lock, flags);
    return -ENOSPC; /* 表已满 */
}

/**
 * 根据 handle 查找锁
 *
 * @param table  同步对象表
 * @param handle 槽位索引
 * @return 锁指针,未找到返回 NULL
 */
static mutex_t *sync_table_get_mutex(struct sync_table *table, uint32_t handle) {
    if (handle >= CFG_PROCESS_MUTEX_SLOTS) {
        return NULL;
    }

    uint32_t flags = spin_lock_irqsave(&table->lock);
    uint32_t bit   = 1u << handle;
    mutex_t *m     = (table->mutex_bitmap & bit) ? table->mutexes[handle] : NULL;
    spin_unlock_irqrestore(&table->lock, flags);
    return m;
}

/**
 * 从表中移除锁(用于销毁)
 *
 * @param table  同步对象表
 * @param handle 槽位索引
 * @return 锁指针,未找到返回 NULL
 */
static mutex_t *sync_table_take_mutex(struct sync_table *table, uint32_t handle) {
    if (handle >= CFG_PROCESS_MUTEX_SLOTS) {
        return NULL;
    }

    uint32_t flags = spin_lock_irqsave(&table->lock);
    uint32_t bit   = 1u << handle;
    if (!(table->mutex_bitmap & bit)) {
        spin_unlock_irqrestore(&table->lock, flags);
        return NULL; /* 槽位未使用 */
    }

    mutex_t *m             = table->mutexes[handle];
    table->mutexes[handle] = NULL;
    table->mutex_bitmap &= ~bit;
    spin_unlock_irqrestore(&table->lock, flags);
    return m;
}

/**
 * SYS_MUTEX_CREATE - 创建互斥锁
 *
 * 在内核创建互斥锁并分配 handle.
 *
 * @return handle(非负整数),失败返回负错误码
 */
static int32_t sys_mutex_create(const uint32_t *args) {
    (void)args;

    struct process *proc = process_get_current();
    if (!proc || !proc->sync_table) {
        return -EINVAL;
    }

    /* 创建内核互斥锁 */
    mutex_t *m = mutex_create();
    if (!m) {
        return -ENOMEM;
    }

    /* 分配 handle */
    int handle = sync_table_alloc_mutex(proc->sync_table, m);
    if (handle < 0) {
        mutex_destroy(m);
        return handle; /* 表已满或其他错误 */
    }

    return (int32_t)handle;
}

/**
 * SYS_MUTEX_DESTROY - 销毁互斥锁
 *
 * @param args[0] handle 锁句柄
 * @return 0 成功,负错误码失败
 */
static int32_t sys_mutex_destroy(const uint32_t *args) {
    uint32_t handle = args[0];

    struct process *proc = process_get_current();
    if (!proc || !proc->sync_table) {
        return -EINVAL;
    }

    /* 从表中移除锁 */
    mutex_t *m = sync_table_take_mutex(proc->sync_table, handle);
    if (!m) {
        return -EINVAL; /* handle 无效 */
    }

    mutex_destroy(m);
    return 0;
}

/**
 * SYS_MUTEX_LOCK - 获取锁
 *
 * 如果锁已被占用,当前线程将阻塞等待.
 *
 * @param args[0] handle 锁句柄
 * @return 0 成功,负错误码失败
 */
static int32_t sys_mutex_lock(const uint32_t *args) {
    uint32_t handle = args[0];

    struct process *proc = process_get_current();
    if (!proc || !proc->sync_table) {
        return -EINVAL;
    }

    mutex_t *m = sync_table_get_mutex(proc->sync_table, handle);
    if (!m) {
        return -EINVAL; /* handle 无效 */
    }

    mutex_lock(m); /* 可能阻塞 */
    return 0;
}

/**
 * SYS_MUTEX_UNLOCK - 释放锁
 *
 * @param args[0] handle 锁句柄
 * @return 0 成功,负错误码失败
 */
static int32_t sys_mutex_unlock(const uint32_t *args) {
    uint32_t handle = args[0];

    struct process *proc = process_get_current();
    if (!proc || !proc->sync_table) {
        return -EINVAL;
    }

    mutex_t *m = sync_table_get_mutex(proc->sync_table, handle);
    if (!m) {
        return -EINVAL; /* handle 无效 */
    }

    mutex_unlock(m);
    return 0;
}

/**
 * 注册同步原语系统调用
 */
void sys_sync_init(void) {
    syscall_register(SYS_MUTEX_CREATE, sys_mutex_create, 0, "mutex_create");
    syscall_register(SYS_MUTEX_DESTROY, sys_mutex_destroy, 1, "mutex_destroy");
    syscall_register(SYS_MUTEX_LOCK, sys_mutex_lock, 1, "mutex_lock");
    syscall_register(SYS_MUTEX_UNLOCK, sys_mutex_unlock, 1, "mutex_unlock");
}
