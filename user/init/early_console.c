/**
 * @file early_console.c
 * @brief Early console output using SYS_DEBUG_PUT
 *
 * Used by init before seriald is available. Requires xnix.debug.console permission.
 */

#include "early_console.h"

#include <stdint.h>
#include <xnix/abi/syscall.h>

static bool g_early_mode = true;

void early_console_disable(void) {
    g_early_mode = false;
}

bool early_console_is_active(void) {
    return g_early_mode;
}

void early_putc(char c) {
    if (!g_early_mode) {
        return;
    }

    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DEBUG_PUT), "b"((uint32_t)c) : "memory");
    (void)ret;
}

void early_puts(const char *s) {
    if (!s || !g_early_mode) {
        return;
    }

    while (*s) {
        early_putc(*s++);
    }
}

void early_set_color(uint8_t fg, uint8_t bg) {
    if (!g_early_mode) {
        return;
    }

    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(SYS_DEBUG_SET_COLOR), "b"((uint32_t)fg), "c"((uint32_t)bg)
                 : "memory");
    (void)ret;
}

void early_reset_color(void) {
    if (!g_early_mode) {
        return;
    }

    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_DEBUG_RESET_COLOR) : "memory");
    (void)ret;
}
