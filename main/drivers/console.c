/**
 * @file console.c
 * @brief 控制台驱动框架
 * @author XiaoXiu
 * @date 2026-01-22
 */

#include <xnix/console.h>
#include <xnix/string.h>

#define MAX_CONSOLES 4

static struct console *consoles[MAX_CONSOLES];
static int             console_count = 0;

int console_register(struct console *c) {
    if (console_count >= MAX_CONSOLES || !c) {
        return -1;
    }
    consoles[console_count++] = c;
    return 0;
}

int console_replace(const char *name, struct console *c) {
    if (!name || !c) {
        return -1;
    }

    for (int i = 0; i < console_count; i++) {
        if (consoles[i] && consoles[i]->name && !strcmp(consoles[i]->name, name)) {
            consoles[i] = c;
            return 0;
        }
    }

    return -1;
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
