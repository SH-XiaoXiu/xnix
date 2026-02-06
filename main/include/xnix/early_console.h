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
 * 颜色控制是可选能力,用于日志级别前缀等场景.
 * 无异步,无消费者线程.
 */
struct early_console_backend {
    const char *name;
    void (*init)(void);
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*clear)(void);
    void (*set_color)(uint8_t fg, uint8_t bg);
    void (*reset_color)(void);

    struct early_console_backend *next;
};

/**
 * @brief 早期控制台颜色(与 VGA 16 色编号一致)
 */
enum early_console_color {
    EARLY_COLOR_BLACK         = 0,
    EARLY_COLOR_BLUE          = 1,
    EARLY_COLOR_GREEN         = 2,
    EARLY_COLOR_CYAN          = 3,
    EARLY_COLOR_RED           = 4,
    EARLY_COLOR_MAGENTA       = 5,
    EARLY_COLOR_BROWN         = 6,
    EARLY_COLOR_LIGHT_GREY    = 7,
    EARLY_COLOR_DARK_GREY     = 8,
    EARLY_COLOR_LIGHT_BLUE    = 9,
    EARLY_COLOR_LIGHT_GREEN   = 10,
    EARLY_COLOR_LIGHT_CYAN    = 11,
    EARLY_COLOR_LIGHT_RED     = 12,
    EARLY_COLOR_LIGHT_MAGENTA = 13,
    EARLY_COLOR_LIGHT_BROWN   = 14,
    EARLY_COLOR_WHITE         = 15,
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
 * @brief 设置早期控制台颜色(可选能力)
 *
 * 只影响支持颜色的后端,其它后端忽略.
 */
void early_console_set_color(enum early_console_color fg, enum early_console_color bg);

/**
 * @brief 重置早期控制台颜色(可选能力)
 */
void early_console_reset_color(void);

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
