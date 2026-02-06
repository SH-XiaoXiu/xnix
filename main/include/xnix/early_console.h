/**
 * @file early_console.h
 * @brief 早期/紧急控制台 API
 *
 * 极简的同步控制台,用于启动早期和 panic 场景.
 * 所有输出直接写入硬件,无异步队列,无 ring buffer.
 * 用户态控制台就绪后通过 early_console_disable() 关闭.
 */

#ifndef XNIX_EARLY_CONSOLE_H
#define XNIX_EARLY_CONSOLE_H

#include <xnix/types.h>

/**
 * 早期控制台后端
 *
 * 只提供最基本的输出能力:putc,puts,clear.
 * 无颜色,无异步,无消费者线程.
 */
struct early_console_backend {
    const char *name;
    void (*init)(void);
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*clear)(void);

    struct early_console_backend *next;
};

/**
 * 注册一个早期控制台后端
 *
 * @param be 后端对象(静态或长期驻留内存)
 * @return 0 成功,-1 失败
 */
int early_console_register(struct early_console_backend *be);

/**
 * 初始化所有已注册后端
 */
void early_console_init(void);

/**
 * 输出单字符到所有后端
 */
void early_putc(char c);

/**
 * 输出字符串到所有后端
 */
void early_puts(const char *s);

/**
 * 清屏
 */
void early_clear(void);

/**
 * 禁用早期控制台(用户态控制台就绪后调用)
 *
 * 之后 early_putc/early_puts 不再输出.
 */
void early_console_disable(void);

/**
 * 紧急模式(panic 时调用)
 *
 * 强制重新启用早期控制台,绕过所有锁,直接写硬件.
 */
void early_console_emergency(void);

/**
 * 查询早期控制台是否仍然活跃
 */
bool early_console_is_active(void);

#endif /* XNIX_EARLY_CONSOLE_H */
