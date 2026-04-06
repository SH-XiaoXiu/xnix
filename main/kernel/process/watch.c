/**
 * @file watch.c
 * @brief 进程生命周期 watch 对象
 */

#include "process_internal.h"

#include <arch/cpu.h>

#include <ipc/notification.h>
#include <xnix/handle.h>
#include <xnix/mm.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>

struct process_watch {
    uint32_t                 refcount;
    struct process_watch    *next;
    struct process          *target;
    struct ipc_notification *notif;
    uint32_t                 bits;
    uint8_t                  armed;
};

static spinlock_t            g_process_watch_lock;
static struct process_watch *g_process_watch_list;

static void process_watch_unlink_locked(struct process_watch *watch) {
    struct process_watch **pp = &g_process_watch_list;

    while (*pp) {
        if (*pp == watch) {
            *pp       = watch->next;
            watch->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }
}

void process_watch_ref(void *ptr) {
    struct process_watch *watch = ptr;
    uint32_t              flags;

    if (!watch) {
        return;
    }

    flags = cpu_irq_save();
    watch->refcount++;
    cpu_irq_restore(flags);
}

void process_watch_unref(void *ptr) {
    struct process_watch    *watch = ptr;
    struct process          *target = NULL;
    struct ipc_notification *notif = NULL;
    uint32_t                 flags;
    uint8_t                  free_now = 0;

    if (!watch) {
        return;
    }

    flags = cpu_irq_save();
    spin_lock(&g_process_watch_lock);

    if (watch->armed) {
        process_watch_unlink_locked(watch);
        watch->armed = 0;
    }

    watch->refcount--;
    if (watch->refcount == 0) {
        target       = watch->target;
        notif        = watch->notif;
        watch->target = NULL;
        watch->notif  = NULL;
        free_now      = 1;
    }

    spin_unlock(&g_process_watch_lock);
    cpu_irq_restore(flags);

    if (!free_now) {
        return;
    }

    if (target) {
        process_unref(target);
    }
    if (notif) {
        notification_unref(notif);
    }
    kfree(watch);
}

void process_watch_subsystem_init(void) {
    spin_init(&g_process_watch_lock);
    g_process_watch_list = NULL;
}

handle_t process_watch_create(struct process *owner, pid_t pid, handle_t notif_handle, uint32_t bits) {
    struct process          *target;
    struct handle_entry      notif_entry;
    struct ipc_notification *notif;
    struct process_watch    *watch;
    uint32_t                 flags;
    handle_t                 watch_handle;

    if (!owner || pid <= 0 || bits == 0) {
        return HANDLE_INVALID;
    }

    target = process_find_by_pid(pid);
    if (!target) {
        return HANDLE_INVALID;
    }

    if (handle_acquire(owner, notif_handle, HANDLE_NOTIFICATION, &notif_entry) < 0) {
        process_unref(target);
        return HANDLE_INVALID;
    }
    notif = notif_entry.object;

    watch = kzalloc(sizeof(*watch));
    if (!watch) {
        handle_object_put(notif_entry.type, notif_entry.object);
        process_unref(target);
        return HANDLE_INVALID;
    }

    watch->refcount = 1;
    watch->target   = target;
    watch->notif    = notif;
    watch->bits     = bits;
    watch->armed    = 1;

    flags = cpu_irq_save();
    spin_lock(&g_process_watch_lock);

    if (target->state == PROCESS_ZOMBIE) {
        watch->armed  = 0;
        watch->target = NULL;
        spin_unlock(&g_process_watch_lock);
        cpu_irq_restore(flags);

        notification_signal_by_ptr(notif, bits);
        process_unref(target);
    } else {
        watch->next          = g_process_watch_list;
        g_process_watch_list = watch;
        spin_unlock(&g_process_watch_lock);
        cpu_irq_restore(flags);
    }

    watch_handle = handle_alloc(owner, HANDLE_PROC_WATCH, watch, NULL);
    if (watch_handle == HANDLE_INVALID) {
        process_watch_unref(watch);
        return HANDLE_INVALID;
    }

    return watch_handle;
}

void process_watch_signal_exit(struct process *proc) {
    struct process_watch *watch;
    uint32_t              flags;

    if (!proc) {
        return;
    }

    flags = cpu_irq_save();
    spin_lock(&g_process_watch_lock);

    watch = g_process_watch_list;
    while (watch) {
        struct process_watch *next = watch->next;

        if (watch->armed && watch->target == proc) {
            struct process          *target = watch->target;
            struct ipc_notification *notif  = watch->notif;
            uint32_t                 bits   = watch->bits;

            process_watch_unlink_locked(watch);
            watch->armed  = 0;
            watch->target = NULL;

            if (notif && bits) {
                notification_signal_by_ptr(notif, bits);
            }
            if (target) {
                process_unref(target);
            }
        }

        watch = next;
    }

    spin_unlock(&g_process_watch_lock);
    cpu_irq_restore(flags);
}
