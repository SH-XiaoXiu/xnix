/**
 * @file early_console.h
 * @brief 早期控制台接口
 */

#ifndef INIT_EARLY_CONSOLE_H
#define INIT_EARLY_CONSOLE_H

#include <stdbool.h>

/**
 * 禁用早期控制台(切换到基于IPC的输出)
 */
void early_console_disable(void);

/**
 * 检查早期控制台是否处于活动状态
 */
bool early_console_is_active(void);

/**
 * 输出单个字符(通过 SYS_DEBUG_PUT)
 */
void early_putc(char c);

/**
 * 输出字符串(通过 SYS_DEBUG_PUT)
 */
void early_puts(const char *s);

#endif /* INIT_EARLY_CONSOLE_H */
