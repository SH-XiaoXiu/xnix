/**
 * @file driver_select.c
 * @brief 驱动注册与选择框架实现
 *
 * 实现 Boot 裁切的驱动选择逻辑:
 * - 驱动自注册到链表
 * - 按优先级排序
 * - 支持命令行偏好
 * - probe() 检测硬件支持
 */

#include <xnix/driver.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/*
 * IRQ 控制器驱动管理
 */

static struct irqchip_driver *irqchip_drivers = NULL;
static struct irqchip_driver *irqchip_current = NULL;

void irqchip_register(struct irqchip_driver *drv) {
    if (!drv) {
        return;
    }

    /* 插入链表(按优先级降序) */
    drv->next = NULL;

    if (!irqchip_drivers || drv->priority > irqchip_drivers->priority) {
        drv->next       = irqchip_drivers;
        irqchip_drivers = drv;
        return;
    }

    struct irqchip_driver *p = irqchip_drivers;
    while (p->next && p->next->priority >= drv->priority) {
        p = p->next;
    }
    drv->next = p->next;
    p->next   = drv;
}

struct irqchip_driver *irqchip_select(const char *prefer) {
    struct irqchip_driver *best = NULL;

    for (struct irqchip_driver *d = irqchip_drivers; d; d = d->next) {
        /* 命令行偏好优先 */
        if (prefer && strcmp(d->name, prefer) == 0) {
            if (d->probe && d->probe()) {
                return d;
            }
            /* 偏好驱动不可用,继续寻找其他 */
            pr_warn("irqchip: preferred '%s' not available", prefer);
        }

        /* 选择优先级最高且 probe 成功的 */
        if (!d->probe || d->probe()) {
            if (!best) {
                best = d;
            }
            /* 由于链表已按优先级排序,第一个 probe 成功的就是最佳 */
            break;
        }
    }

    return best;
}

void irqchip_select_and_init(const char *prefer) {
    struct irqchip_driver *drv = irqchip_select(prefer);
    if (!drv) {
        pr_err("irqchip: no available driver!");
        return;
    }

    irqchip_current = drv;
    pr_info("irqchip: selected '%s' (priority %d)", drv->name, drv->priority);

    if (drv->init) {
        drv->init();
    }
}

struct irqchip_driver *irqchip_get_current(void) {
    return irqchip_current;
}

/*
 * 定时器驱动管理
 */

static struct timer_driver_ext *timer_drivers = NULL;
static struct timer_driver_ext *timer_current = NULL;

void timer_drv_register(struct timer_driver_ext *drv) {
    if (!drv) {
        return;
    }

    /* 插入链表(按优先级降序) */
    drv->next = NULL;

    if (!timer_drivers || drv->priority > timer_drivers->priority) {
        drv->next     = timer_drivers;
        timer_drivers = drv;
        return;
    }

    struct timer_driver_ext *p = timer_drivers;
    while (p->next && p->next->priority >= drv->priority) {
        p = p->next;
    }
    drv->next = p->next;
    p->next   = drv;
}

struct timer_driver_ext *timer_drv_select(const char *prefer) {
    struct timer_driver_ext *best = NULL;

    for (struct timer_driver_ext *d = timer_drivers; d; d = d->next) {
        /* 命令行偏好优先 */
        if (prefer && strcmp(d->name, prefer) == 0) {
            if (d->probe && d->probe()) {
                return d;
            }
            pr_warn("timer: preferred '%s' not available", prefer);
        }

        /* 选择优先级最高且 probe 成功的 */
        if (!d->probe || d->probe()) {
            if (!best) {
                best = d;
            }
            break;
        }
    }

    return best;
}

void timer_drv_select_best(const char *prefer) {
    struct timer_driver_ext *drv = timer_drv_select(prefer);
    if (!drv) {
        pr_err("timer: no available driver!");
        return;
    }

    timer_current = drv;
    pr_info("timer: selected '%s' (priority %d)", drv->name, drv->priority);
}

struct timer_driver_ext *timer_drv_get_current(void) {
    return timer_current;
}

/*
 * 命令行参数辅助函数
 */

/* 静态缓冲区用于存储命令行值 */
static char cmdline_value_buf[64];

/* 保存的命令行指针(由 boot.c 设置) */
static const char *saved_cmdline = NULL;

/**
 * 保存命令行指针供后续查询
 * 由 boot_init() 调用
 */
void boot_save_cmdline(const char *cmdline) {
    saved_cmdline = cmdline;
}

const char *boot_get_cmdline_value(const char *key) {
    if (!saved_cmdline || !key) {
        return NULL;
    }

    size_t      key_len = strlen(key);
    const char *p       = saved_cmdline;

    while (*p) {
        /* 跳过空格 */
        while (*p == ' ') {
            p++;
        }

        /* 检查是否匹配 key= */
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *v = p + key_len + 1;
            size_t      i = 0;

            /* 复制值到缓冲区 */
            while (*v && *v != ' ' && i < sizeof(cmdline_value_buf) - 1) {
                cmdline_value_buf[i++] = *v++;
            }
            cmdline_value_buf[i] = '\0';
            return cmdline_value_buf;
        }

        /* 跳到下一个参数 */
        while (*p && *p != ' ') {
            p++;
        }
    }

    return NULL;
}
