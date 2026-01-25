/**
 * @file console.c
 * @brief 控制台驱动框架
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <drivers/console.h>

#define MAX_CONSOLES 4

static struct console_driver *consoles[MAX_CONSOLES];
static int                    console_count = 0;

int console_register(struct console_driver *drv) {
    if (console_count >= MAX_CONSOLES || !drv) {
        return -1;
    }
    consoles[console_count++] = drv;
    return 0;
}

void console_init(void) {
    for (int i = 0; i < console_count; i++) {
        if (consoles[i]->init) {
            consoles[i]->init();
        }
    }
}

void console_putc(char c) {
    for (int i = 0; i < console_count; i++) {
        if (consoles[i]->putc) {
            consoles[i]->putc(c);
        }
    }
}

void console_puts(const char *s) {
    for (int i = 0; i < console_count; i++) {
        if (consoles[i]->puts) {
            consoles[i]->puts(s);
        }
    }
}

void console_set_color(kcolor_t color) {
    for (int i = 0; i < console_count; i++) {
        if (consoles[i]->set_color) {
            consoles[i]->set_color(color);
        }
    }
}

void console_reset_color(void) {
    for (int i = 0; i < console_count; i++) {
        if (consoles[i]->reset_color) {
            consoles[i]->reset_color();
        }
    }
}

void console_clear(void) {
    for (int i = 0; i < console_count; i++) {
        if (consoles[i]->clear) {
            consoles[i]->clear();
        }
    }
}
