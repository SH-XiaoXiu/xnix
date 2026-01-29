/**
 * @file kernel/sys/sys_sync.c
 * @brief 同步原语系统调用(互斥锁)
 */

#include <kernel/process/process.h>
#include <kernel/sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/sync.h>
#include <xnix/syscall.h>

static int sync_table_alloc_mutex(struct sync_table *table, mutex_t *m) {
    uint32_t flags = spin_lock_irqsave(&table->lock);

    for (uint32_t i = 0; i < 32; i++) {
        uint32_t bit = 1u << i;
        if (table->mutex_bitmap & bit) {
            continue;
        }
        table->mutex_bitmap |= bit;
        table->mutexes[i] = m;
        spin_unlock_irqrestore(&table->lock, flags);
        return (int)i;
    }

    spin_unlock_irqrestore(&table->lock, flags);
    return -ENOSPC;
}

static mutex_t *sync_table_get_mutex(struct sync_table *table, uint32_t handle) {
    if (handle >= 32) {
        return NULL;
    }

    uint32_t flags = spin_lock_irqsave(&table->lock);
    uint32_t bit   = 1u << handle;
    mutex_t *m     = (table->mutex_bitmap & bit) ? table->mutexes[handle] : NULL;
    spin_unlock_irqrestore(&table->lock, flags);
    return m;
}

static mutex_t *sync_table_take_mutex(struct sync_table *table, uint32_t handle) {
    if (handle >= 32) {
        return NULL;
    }

    uint32_t flags = spin_lock_irqsave(&table->lock);
    uint32_t bit   = 1u << handle;
    if (!(table->mutex_bitmap & bit)) {
        spin_unlock_irqrestore(&table->lock, flags);
        return NULL;
    }

    mutex_t *m             = table->mutexes[handle];
    table->mutexes[handle] = NULL;
    table->mutex_bitmap &= ~bit;
    spin_unlock_irqrestore(&table->lock, flags);
    return m;
}

static int32_t sys_mutex_create(const uint32_t *args) {
    (void)args;

    struct process *proc = process_get_current();
    if (!proc || !proc->sync_table) {
        return -EINVAL;
    }

    mutex_t *m = mutex_create();
    if (!m) {
        return -ENOMEM;
    }

    int handle = sync_table_alloc_mutex(proc->sync_table, m);
    if (handle < 0) {
        mutex_destroy(m);
        return handle;
    }

    return (int32_t)handle;
}

static int32_t sys_mutex_destroy(const uint32_t *args) {
    uint32_t handle = args[0];

    struct process *proc = process_get_current();
    if (!proc || !proc->sync_table) {
        return -EINVAL;
    }

    mutex_t *m = sync_table_take_mutex(proc->sync_table, handle);
    if (!m) {
        return -EINVAL;
    }

    mutex_destroy(m);
    return 0;
}

static int32_t sys_mutex_lock(const uint32_t *args) {
    uint32_t handle = args[0];

    struct process *proc = process_get_current();
    if (!proc || !proc->sync_table) {
        return -EINVAL;
    }

    mutex_t *m = sync_table_get_mutex(proc->sync_table, handle);
    if (!m) {
        return -EINVAL;
    }

    mutex_lock(m);
    return 0;
}

static int32_t sys_mutex_unlock(const uint32_t *args) {
    uint32_t handle = args[0];

    struct process *proc = process_get_current();
    if (!proc || !proc->sync_table) {
        return -EINVAL;
    }

    mutex_t *m = sync_table_get_mutex(proc->sync_table, handle);
    if (!m) {
        return -EINVAL;
    }

    mutex_unlock(m);
    return 0;
}

void sys_sync_init(void) {
    syscall_register(SYS_MUTEX_CREATE, sys_mutex_create, 0, "mutex_create");
    syscall_register(SYS_MUTEX_DESTROY, sys_mutex_destroy, 1, "mutex_destroy");
    syscall_register(SYS_MUTEX_LOCK, sys_mutex_lock, 1, "mutex_lock");
    syscall_register(SYS_MUTEX_UNLOCK, sys_mutex_unlock, 1, "mutex_unlock");
}
