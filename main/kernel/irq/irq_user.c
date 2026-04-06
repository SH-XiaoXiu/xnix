/**
 * @file irq_user.c
 * @brief IRQ 用户态绑定实现
 *
 * 允许用户态进程接收 IRQ 通知并读取 IRQ 数据.
 */

#include <asm/irq.h>
#include <ipc/notification.h>
#include <xnix/config.h>
#include <xnix/errno.h>
#include <xnix/irq.h>
#include <xnix/process.h>
#include <xnix/sync.h>
#include <xnix/thread_def.h>
#include <xnix/usraccess.h>

#define IRQ_USER_MAX_BINDINGS 4

/**
 * IRQ 用户态绑定信息
 */
struct irq_user_binding {
    bool                     bound;       /* 是否已绑定 */
    struct process          *owner;       /* 绑定所属进程 */
    struct ipc_notification *notif;       /* 目标 notification(可选) */
    uint32_t                 signal_bits; /* 触发时发送的信号位 */

    /* 数据缓冲区(环形) */
    uint8_t  buffer[CFG_IRQ_USER_BUF_SIZE];
    uint16_t head;
    uint16_t tail;

    /* 等待读取的线程 */
    struct thread *waiter;

    spinlock_t lock;
};

static struct irq_user_binding irq_bindings[ARCH_NR_IRQS][IRQ_USER_MAX_BINDINGS];

static bool irq_has_bound_slots(uint8_t irq) {
    for (int i = 0; i < IRQ_USER_MAX_BINDINGS; i++) {
        if (irq_bindings[irq][i].bound) {
            return true;
        }
    }
    return false;
}

int irq_bind_notification(uint8_t irq, struct process *owner,
                          struct ipc_notification *notif, uint32_t bits) {
    if (irq >= ARCH_NR_IRQS) {
        return -EINVAL;
    }

    for (int i = 0; i < IRQ_USER_MAX_BINDINGS; i++) {
        struct irq_user_binding *bind = &irq_bindings[irq][i];

        spin_lock(&bind->lock);

        if (bind->bound && bind->owner == owner) {
            spin_unlock(&bind->lock);
            return -EBUSY;
        }

        if (!bind->bound) {
            bind->bound       = true;
            bind->owner       = owner;
            bind->notif       = notif;
            bind->signal_bits = bits;
            bind->head        = 0;
            bind->tail        = 0;
            bind->waiter      = NULL;

            if (notif) {
                notification_ref(notif);
            }

            spin_unlock(&bind->lock);
            irq_enable(irq);
            return 0;
        }

        spin_unlock(&bind->lock);
    }

    return -EBUSY;
}

int irq_unbind_notification(uint8_t irq, struct process *owner) {
    if (irq >= ARCH_NR_IRQS) {
        return -EINVAL;
    }

    for (int i = 0; i < IRQ_USER_MAX_BINDINGS; i++) {
        struct irq_user_binding *bind = &irq_bindings[irq][i];

        spin_lock(&bind->lock);

        if (!bind->bound || bind->owner != owner) {
            spin_unlock(&bind->lock);
            continue;
        }

        if (bind->notif) {
            notification_unref(bind->notif);
        }
        bind->bound       = false;
        bind->owner       = NULL;
        bind->notif       = NULL;
        bind->signal_bits = 0;
        bind->head        = 0;
        bind->tail        = 0;

        if (bind->waiter) {
            sched_wakeup_thread(bind->waiter);
            bind->waiter = NULL;
        }

        spin_unlock(&bind->lock);

        if (!irq_has_bound_slots(irq)) {
            irq_disable(irq);
        }
        return 0;
    }

    return -ENOENT;
}

void irq_user_push(uint8_t irq, uint8_t data) {
    if (irq >= ARCH_NR_IRQS) {
        return;
    }

    for (int i = 0; i < IRQ_USER_MAX_BINDINGS; i++) {
        struct irq_user_binding *bind = &irq_bindings[irq][i];
        struct ipc_notification *notif;
        uint32_t                 bits;

        spin_lock(&bind->lock);

        if (!bind->bound) {
            spin_unlock(&bind->lock);
            continue;
        }

        {
            uint16_t next = (bind->head + 1) % CFG_IRQ_USER_BUF_SIZE;
            if (next != bind->tail) {
                bind->buffer[bind->head] = data;
                bind->head               = next;
            }
        }

        if (bind->waiter) {
            sched_wakeup_thread(bind->waiter);
            bind->waiter = NULL;
        }

        notif = bind->notif;
        bits  = bind->signal_bits;
        spin_unlock(&bind->lock);

        if (notif) {
            notification_signal_by_ptr(notif, bits);
        }
    }
}

void irq_user_signal(uint8_t irq) {
    if (irq >= ARCH_NR_IRQS) {
        return;
    }

    for (int i = 0; i < IRQ_USER_MAX_BINDINGS; i++) {
        struct irq_user_binding *bind = &irq_bindings[irq][i];
        struct ipc_notification *notif;
        uint32_t                 bits;

        spin_lock(&bind->lock);
        if (!bind->bound) {
            spin_unlock(&bind->lock);
            continue;
        }
        if (bind->waiter) {
            sched_wakeup_thread(bind->waiter);
            bind->waiter = NULL;
        }
        notif = bind->notif;
        bits  = bind->signal_bits;
        spin_unlock(&bind->lock);

        if (notif) {
            notification_signal_by_ptr(notif, bits);
        }
    }
}

int irq_user_read(uint8_t irq, uint8_t *buf, size_t size, bool block) {
    if (irq >= ARCH_NR_IRQS || !buf || size == 0) {
        return -EINVAL;
    }

    struct process          *owner = process_current();
    struct irq_user_binding *bind  = NULL;
    size_t                   count = 0;

    if (!owner) {
        return -ESRCH;
    }

    if (size > CFG_IRQ_USER_BUF_SIZE) {
        size = CFG_IRQ_USER_BUF_SIZE;
    }

    for (int i = 0; i < IRQ_USER_MAX_BINDINGS; i++) {
        if (irq_bindings[irq][i].bound && irq_bindings[irq][i].owner == owner) {
            bind = &irq_bindings[irq][i];
            break;
        }
    }
    if (!bind) {
        return -ENOENT;
    }

    for (;;) {
        uint8_t tmp[64];
        size_t  got = 0;

        spin_lock(&bind->lock);

        if (!bind->bound) {
            spin_unlock(&bind->lock);
            return -ENOENT;
        }

        while (got < sizeof(tmp) && count + got < size && bind->head != bind->tail) {
            tmp[got++] = bind->buffer[bind->tail];
            bind->tail = (bind->tail + 1) % CFG_IRQ_USER_BUF_SIZE;
        }

        if (got == 0) {
            if (count > 0) {
                spin_unlock(&bind->lock);
                break;
            }
            if (!block) {
                spin_unlock(&bind->lock);
                return -EAGAIN;
            }

            bind->waiter = sched_current();
            spin_unlock(&bind->lock);
            sched_block(bind);
            continue;
        }

        spin_unlock(&bind->lock);

        int ret = copy_to_user(buf + count, tmp, got);
        if (ret < 0) {
            return ret;
        }

        count += got;
        block = false;

        if (count >= size) {
            break;
        }
    }

    return (int)count;
}
