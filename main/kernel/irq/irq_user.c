/**
 * @file irq_user.c
 * @brief IRQ 用户态绑定实现
 *
 * 允许用户态进程接收 IRQ 通知并读取 IRQ 数据.
 */

#include <asm/irq.h>
#include <kernel/ipc/notification.h>
#include <kernel/irq/irq.h>
#include <kernel/sched/sched.h>
#include <xnix/config.h>
#include <xnix/errno.h>
#include <xnix/sync.h>

/**
 * IRQ 用户态绑定信息
 */
struct irq_user_binding {
    bool                     bound;       /* 是否已绑定 */
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

static struct irq_user_binding irq_bindings[ARCH_NR_IRQS];

int irq_bind_notification(uint8_t irq, struct ipc_notification *notif, uint32_t bits) {
    if (irq >= ARCH_NR_IRQS) {
        return -EINVAL;
    }

    struct irq_user_binding *bind = &irq_bindings[irq];

    spin_lock(&bind->lock);

    if (bind->bound) {
        spin_unlock(&bind->lock);
        return -EBUSY;
    }

    bind->bound       = true;
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

int irq_unbind_notification(uint8_t irq) {
    if (irq >= ARCH_NR_IRQS) {
        return -EINVAL;
    }

    struct irq_user_binding *bind = &irq_bindings[irq];

    spin_lock(&bind->lock);

    if (!bind->bound) {
        spin_unlock(&bind->lock);
        return -ENOENT;
    }

    irq_disable(irq);

    if (bind->notif) {
        notification_unref(bind->notif);
    }
    bind->bound       = false;
    bind->notif       = NULL;
    bind->signal_bits = 0;

    /* 唤醒等待的线程(如果有) */
    if (bind->waiter) {
        sched_wakeup_thread(bind->waiter);
        bind->waiter = NULL;
    }

    spin_unlock(&bind->lock);

    return 0;
}

void irq_user_push(uint8_t irq, uint8_t data) {
    if (irq >= ARCH_NR_IRQS) {
        return;
    }

    struct irq_user_binding *bind = &irq_bindings[irq];

    spin_lock(&bind->lock);

    if (!bind->bound) {
        spin_unlock(&bind->lock);
        return;
    }

    /* 写入缓冲区 */
    uint16_t next = (bind->head + 1) % CFG_IRQ_USER_BUF_SIZE;
    if (next != bind->tail) {
        bind->buffer[bind->head] = data;
        bind->head               = next;
    }
    /* 缓冲区满则丢弃数据 */

    /* 唤醒等待的线程 */
    if (bind->waiter) {
        sched_wakeup_thread(bind->waiter);
        bind->waiter = NULL;
    }

    struct ipc_notification *notif = bind->notif;
    uint32_t                 bits  = bind->signal_bits;

    spin_unlock(&bind->lock);

    /* 发送通知(如果有) */
    if (notif) {
        notification_signal_by_ptr(notif, bits);
    }
}

int irq_user_read(uint8_t irq, uint8_t *buf, size_t size, bool block) {
    if (irq >= ARCH_NR_IRQS || !buf || size == 0) {
        return -EINVAL;
    }

    struct irq_user_binding *bind  = &irq_bindings[irq];
    size_t                   count = 0;

    spin_lock(&bind->lock);

    if (!bind->bound) {
        spin_unlock(&bind->lock);
        return -ENOENT;
    }

    while (count < size) {
        /* 检查缓冲区是否有数据 */
        if (bind->head != bind->tail) {
            buf[count++] = bind->buffer[bind->tail];
            bind->tail   = (bind->tail + 1) % CFG_IRQ_USER_BUF_SIZE;
        } else if (count > 0) {
            /* 已读到数据,返回 */
            break;
        } else if (!block) {
            /* 非阻塞,无数据返回 */
            spin_unlock(&bind->lock);
            return -EAGAIN;
        } else {
            /* 阻塞等待 */
            bind->waiter = sched_current();
            spin_unlock(&bind->lock);

            sched_block(bind);

            spin_lock(&bind->lock);

            /* 检查是否被解绑 */
            if (!bind->bound) {
                spin_unlock(&bind->lock);
                return -ENOENT;
            }
        }
    }

    spin_unlock(&bind->lock);

    return (int)count;
}
