/**
 * @file kernel/boot/cmdline.c
 * @brief 启动命令行解析
 */

#include "boot_internal.h"

#include <xnix/boot.h>
#include <xnix/string.h>

static const char *g_boot_cmdline = NULL;

static char g_boot_cmdline_value_buf[64];

bool boot_kv_get_value(const char *cmdline, const char *key, char *out, size_t out_sz) {
    size_t key_len;

    if (!cmdline || !key || !out || out_sz == 0) {
        return false;
    }

    key_len = strlen(key);

    const char *p = cmdline;
    while (*p) {
        while (*p == ' ') {
            p++;
        }

        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *v = p + key_len + 1;
            size_t      i = 0;

            while (*v && *v != ' ' && i < out_sz - 1) {
                out[i++] = *v++;
            }
            out[i] = '\0';
            return true;
        }

        while (*p && *p != ' ') {
            p++;
        }
    }

    return false;
}

static uint32_t boot_parse_u32(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return v;
}

void boot_cmdline_set(const char *cmdline) {
    g_boot_cmdline = cmdline;
}

const char *boot_cmdline_get(const char *key) {
    if (!g_boot_cmdline || !key) {
        return NULL;
    }

    if (!boot_kv_get_value(g_boot_cmdline, key, g_boot_cmdline_value_buf,
                           sizeof(g_boot_cmdline_value_buf))) {
        return NULL;
    }

    return g_boot_cmdline_value_buf;
}

bool boot_cmdline_has_kv(const char *key, const char *value) {
    size_t key_len = 0;
    size_t val_len = 0;

    if (!g_boot_cmdline || !key || !value) {
        return false;
    }

    key_len = strlen(key);
    val_len = strlen(value);

    const char *p = g_boot_cmdline;
    while (*p) {
        while (*p == ' ') {
            p++;
        }

        if (!strncmp(p, key, key_len) && p[key_len] == '=' &&
            !strncmp(p + key_len + 1, value, val_len) &&
            ((p + key_len + 1 + val_len)[0] == '\0' || (p + key_len + 1 + val_len)[0] == ' ')) {
            return true;
        }

        while (*p && *p != ' ') {
            p++;
        }
    }

    return false;
}

bool boot_cmdline_get_u32(const char *key, uint32_t *out) {
    size_t key_len;

    if (!g_boot_cmdline || !key || !out) {
        return false;
    }

    key_len = strlen(key);

    const char *p = g_boot_cmdline;
    while (*p) {
        while (*p == ' ') {
            p++;
        }

        if (!strncmp(p, key, key_len) && p[key_len] == '=') {
            const char *v = p + key_len + 1;
            if (*v < '0' || *v > '9') {
                return false;
            }
            *out = boot_parse_u32(v);
            return true;
        }

        while (*p && *p != ' ') {
            p++;
        }
    }

    return false;
}
