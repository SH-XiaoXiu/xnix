/**
 * @file ini_parser.c
 * @brief 简单的 INI 文件解析器实现
 */

#include "ini_parser.h"

#include <d/protocol/vfs.h>
#include <stdio.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/syscall.h>

/**
 * 跳过行首空白字符
 */
static const char *skip_whitespace(const char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

/**
 * 去除字符串尾部空白
 */
static void trim_trailing(char *s) {
    size_t len = strlen(s);
    while (len > 0 &&
           (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

/**
 * 解析一行 INI 内容
 *
 * @param line          输入行(会被修改)
 * @param section       当前 section 缓冲区
 * @param section_size  section 缓冲区大小
 * @param handler       回调函数
 * @param ctx           用户上下文
 * @return true 继续解析,false 停止解析
 */
static bool parse_line(char *line, char *section, size_t section_size, ini_handler_t handler,
                       void *ctx) {
    const char *p = skip_whitespace(line);

    /* 空行或注释 */
    if (*p == '\0' || *p == '#' || *p == ';') {
        return true;
    }

    /* section: [name] */
    if (*p == '[') {
        p++;
        char *end = strchr(p, ']');
        if (end) {
            size_t len = (size_t)(end - p);
            if (len >= section_size) {
                len = section_size - 1;
            }
            memcpy(section, p, len);
            section[len] = '\0';
        }
        return true;
    }

    /* key = value */
    char *eq = strchr(line, '=');
    if (eq) {
        *eq = '\0';

        /* 提取 key */
        char        key[INI_MAX_KEY];
        const char *key_start = skip_whitespace(line);
        size_t      key_len   = strlen(key_start);
        if (key_len >= INI_MAX_KEY) {
            key_len = INI_MAX_KEY - 1;
        }
        memcpy(key, key_start, key_len);
        key[key_len] = '\0';
        trim_trailing(key);

        /* 提取 value */
        char        value[INI_MAX_VALUE];
        const char *val_start = skip_whitespace(eq + 1);
        size_t      val_len   = strlen(val_start);
        if (val_len >= INI_MAX_VALUE) {
            val_len = INI_MAX_VALUE - 1;
        }
        memcpy(value, val_start, val_len);
        value[val_len] = '\0';
        trim_trailing(value);

        /* 调用回调 */
        if (!handler(section, key, value, ctx)) {
            return false;
        }
    }

    return true;
}

int ini_parse_buffer(const char *buf, size_t len, ini_handler_t handler, void *ctx) {
    if (!buf || !handler) {
        return -1;
    }

    char   section[INI_MAX_SECTION] = "";
    char   line[INI_MAX_LINE];
    size_t line_pos = 0;

    for (size_t i = 0; i <= len; i++) {
        char c = (i < len) ? buf[i] : '\n';

        if (c == '\n' || c == '\0') {
            line[line_pos] = '\0';
            if (!parse_line(line, section, sizeof(section), handler, ctx)) {
                return 0; /* 回调请求停止 */
            }
            line_pos = 0;
        } else if (line_pos < INI_MAX_LINE - 1) {
            line[line_pos++] = c;
        }
    }

    return 0;
}

int ini_parse_file(const char *path, ini_handler_t handler, void *ctx) {
    if (!path || !handler) {
        return -1;
    }

    /* 打开文件 */
    int fd = vfs_open(path, 0); /* VFS_O_RDONLY */
    if (fd < 0) {
        return fd;
    }

    /* 读取文件内容 (最大 4KB) */
    static char file_buf[4 * 1024];
    int         bytes_read = vfs_read(fd, file_buf, sizeof(file_buf));
    vfs_close(fd);

    if (bytes_read < 0) {
        return bytes_read;
    }

    if (bytes_read == 0) {
        return 0;
    }
    return ini_parse_buffer(file_buf, (size_t)bytes_read, handler, ctx);
}
