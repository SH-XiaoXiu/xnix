/**
 * @file early_console.h
 * @brief Early console output API
 */

#ifndef INIT_EARLY_CONSOLE_H
#define INIT_EARLY_CONSOLE_H

#include <stdbool.h>

/**
 * Disable early console (switch to IPC-based output)
 */
void early_console_disable(void);

/**
 * Check if early console is active
 */
bool early_console_is_active(void);

/**
 * Output a single character (via SYS_DEBUG_PUT)
 */
void early_putc(char c);

/**
 * Output a string (via SYS_DEBUG_PUT)
 */
void early_puts(const char *s);

#endif /* INIT_EARLY_CONSOLE_H */
