/**
 * @file console.h
 * @brief 控制台驱动接口
 * @author XiaoXiu
 * @date 2026-01-22
 */

#ifndef DRIVERS_CONSOLE_H
#define DRIVERS_CONSOLE_H

#include <xnix/types.h>

/**
 * @brief 控制台驱动操作接口
 */
struct console_driver {
    const char *name;
    void (*init)(void);
    void (*putc)(char c);
    void (*puts)(const char *s);
    void (*set_color)(kcolor_t color);
    void (*reset_color)(void);
    void (*clear)(void);
};

/**
 * @brief 注册控制台驱动
 * @param drv 驱动结构指针
 * @return 0 成功,-1 失败
 */
int console_register(struct console_driver *drv);

/**
 * @brief 初始化所有已注册的控制台
 */
void console_init(void);

/**
 * @brief 输出字符到所有控制台
 */
void console_putc(char c);

/**
 * @brief 输出字符串到所有控制台
 */
void console_puts(const char *s);

/**
 * @brief 设置所有控制台颜色
 */
void console_set_color(kcolor_t color);

/**
 * @brief 重置所有控制台颜色
 */
void console_reset_color(void);

/**
 * @brief 清屏
 */
void console_clear(void);

#endif
