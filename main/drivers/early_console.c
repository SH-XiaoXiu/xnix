/**
 * @file early_console.c
 * @brief 早期/紧急控制台实现
 *
 * 纯同步输出到所有注册的后端.无 ring buffer,无消费者线程.
 * 启动早期和 panic 时使用,用户态就绪后禁用.
 */

#include <xnix/early_console.h>
#include <xnix/sync.h>

/* 后端链表 */
static struct early_console_backend *ec_list  = NULL;
static struct early_console_backend *ec_tail  = NULL;
static int                           ec_count = 0;

/* 状态 */
static bool ec_active    = true;
static bool ec_emergency = false;

/* 硬件输出锁 */
static spinlock_t early_hw_lock = SPINLOCK_INIT;

int early_console_register(struct early_console_backend *be) {
    if (!be) {
        return -1;
    }

    be->next = NULL;

    if (!ec_list) {
        ec_list = be;
        ec_tail = be;
    } else {
        ec_tail->next = be;
        ec_tail       = be;
    }
    ec_count++;
    return 0;
}

void early_console_init(void) {
    for (struct early_console_backend *be = ec_list; be; be = be->next) {
        if (be->init) {
            be->init();
        }
    }
}

void early_putc(char c) {
    if (!ec_active && !ec_emergency) {
        return;
    }

    if (ec_emergency) {
        /* 紧急模式:绕过锁,直接写硬件 */
        for (struct early_console_backend *be = ec_list; be; be = be->next) {
            if (be->putc) {
                be->putc(c);
            }
        }
        return;
    }

    uint32_t flags = spin_lock_irqsave(&early_hw_lock);
    for (struct early_console_backend *be = ec_list; be; be = be->next) {
        if (be->putc) {
            be->putc(c);
        }
    }
    spin_unlock_irqrestore(&early_hw_lock, flags);
}

void early_puts(const char *s) {
    if (!s) {
        return;
    }
    if (!ec_active && !ec_emergency) {
        return;
    }

    if (ec_emergency) {
        for (struct early_console_backend *be = ec_list; be; be = be->next) {
            if (be->puts) {
                be->puts(s);
            } else if (be->putc) {
                const char *p = s;
                while (*p) {
                    be->putc(*p++);
                }
            }
        }
        return;
    }

    uint32_t flags = spin_lock_irqsave(&early_hw_lock);
    for (struct early_console_backend *be = ec_list; be; be = be->next) {
        if (be->puts) {
            be->puts(s);
        } else if (be->putc) {
            const char *p = s;
            while (*p) {
                be->putc(*p++);
            }
        }
    }
    spin_unlock_irqrestore(&early_hw_lock, flags);
}

void early_clear(void) {
    if (!ec_active && !ec_emergency) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&early_hw_lock);
    for (struct early_console_backend *be = ec_list; be; be = be->next) {
        if (be->clear) {
            be->clear();
        }
    }
    spin_unlock_irqrestore(&early_hw_lock, flags);
}

void early_console_disable(void) {
    ec_active = false;
}

void early_console_emergency(void) {
    ec_emergency = true;
    ec_active    = true;
}

bool early_console_is_active(void) {
    return ec_active;
}
