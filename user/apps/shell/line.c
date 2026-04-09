/**
 * @file line.c
 * @brief Shell 行编辑器
 *
 * RAW 模式下的行编辑, Tab 补全, 命令历史.
 */

#include "line.h"
#include "path.h"

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/protocol/tty.h>
#include <xnix/protocol/vfs.h>

/* ---- 状态 ---- */

char g_history_file[128] = HISTORY_FILE_DEFAULT;
static char g_history[HISTORY_MAX][LINE_MAX_LEN];
static int  g_history_count;

/* 当前编辑状态 */
static char g_buf[LINE_MAX_LEN];
static int  g_len;
static int  g_cursor;
static int  g_hist_pos;                /* -1 = 当前行 */
static char g_saved[LINE_MAX_LEN];     /* 浏览历史时暂存 */

/* ---- 终端输出 ---- */

static void out_char(char c) {
    write(STDOUT_FILENO, &c, 1);
}

static void out_str(const char *s, int n) {
    if (n > 0) write(STDOUT_FILENO, s, (size_t)n);
}

static void out_cstr(const char *s) {
    out_str(s, (int)strlen(s));
}

/* ---- 重绘 ---- */

static int g_prompt_len;

/**
 * 从光标位置重绘到行尾, 然后把光标移回正确位置
 */
static void refresh_from_cursor(void) {
    /* 输出从 cursor 到 len 的内容 */
    out_str(g_buf + g_cursor, g_len - g_cursor);
    /* 清除旧残留: 输出一个空格再退回 */
    out_char(' ');
    /* 把光标移回: 需要退 (g_len - g_cursor + 1) 步 */
    int back = g_len - g_cursor + 1;
    for (int i = 0; i < back; i++) out_char('\b');
}

/**
 * 完全重绘当前行
 */
static void redraw_line(const char *prompt) {
    /* 回到行首 */
    out_char('\r');
    /* 输出 prompt + 内容 */
    out_cstr(prompt);
    out_str(g_buf, g_len);
    /* 清除行尾残留 */
    out_cstr("\033[K");
    /* 把光标移到正确位置 */
    int back = g_len - g_cursor;
    for (int i = 0; i < back; i++) out_char('\b');
}

/* ---- 编辑操作 ---- */

static void insert_char(char c, const char *prompt __attribute__((unused))) {
    if (g_len >= LINE_MAX_LEN - 1) return;

    if (g_cursor == g_len) {
        /* 在末尾追加 */
        g_buf[g_len++] = c;
        g_buf[g_len] = '\0';
        g_cursor++;
        out_char(c);
    } else {
        /* 在中间插入 */
        memmove(g_buf + g_cursor + 1, g_buf + g_cursor, (size_t)(g_len - g_cursor));
        g_buf[g_cursor] = c;
        g_len++;
        g_buf[g_len] = '\0';
        g_cursor++;
        refresh_from_cursor();
    }
}

static void delete_char(const char *prompt __attribute__((unused))) {
    if (g_cursor <= 0) return;

    if (g_cursor == g_len) {
        /* 删末尾 */
        g_cursor--;
        g_len--;
        g_buf[g_len] = '\0';
        out_cstr("\b \b");
    } else {
        /* 删中间 */
        g_cursor--;
        memmove(g_buf + g_cursor, g_buf + g_cursor + 1, (size_t)(g_len - g_cursor - 1));
        g_len--;
        g_buf[g_len] = '\0';
        out_char('\b');
        refresh_from_cursor();
    }
}

static void move_left(void) {
    if (g_cursor > 0) {
        g_cursor--;
        out_char('\b');
    }
}

static void move_right(void) {
    if (g_cursor < g_len) {
        out_char(g_buf[g_cursor]);
        g_cursor++;
    }
}

/* ---- 历史 ---- */

static void set_line(const char *s, const char *prompt) {
    g_len = (int)strlen(s);
    if (g_len >= LINE_MAX_LEN) g_len = LINE_MAX_LEN - 1;
    memcpy(g_buf, s, (size_t)g_len);
    g_buf[g_len] = '\0';
    g_cursor = g_len;
    redraw_line(prompt);
}

static void history_up(const char *prompt) {
    if (g_history_count == 0) return;

    if (g_hist_pos == -1) {
        /* 保存当前行 */
        memcpy(g_saved, g_buf, (size_t)(g_len + 1));
        g_hist_pos = g_history_count - 1;
    } else if (g_hist_pos > 0) {
        g_hist_pos--;
    } else {
        return; /* 已经到最旧 */
    }

    set_line(g_history[g_hist_pos], prompt);
}

static void history_down(const char *prompt) {
    if (g_hist_pos == -1) return;

    if (g_hist_pos < g_history_count - 1) {
        g_hist_pos++;
        set_line(g_history[g_hist_pos], prompt);
    } else {
        /* 回到当前行 */
        g_hist_pos = -1;
        set_line(g_saved, prompt);
    }
}

/* ---- Tab 补全 ---- */

#define COMPLETE_MAX 32

struct completion {
    char names[COMPLETE_MAX][64];
    int  count;
};

static void complete_add(struct completion *c, const char *name) {
    if (c->count >= COMPLETE_MAX) return;
    strncpy(c->names[c->count], name, 63);
    c->names[c->count][63] = '\0';
    c->count++;
}

/**
 * 计算所有匹配项的最长公共前缀长度 (从 prefix_len 开始)
 */
static int common_prefix_len(struct completion *c, int prefix_len) {
    if (c->count <= 1) return (c->count == 1) ? (int)strlen(c->names[0]) : prefix_len;

    int len = (int)strlen(c->names[0]);
    for (int i = 1; i < c->count; i++) {
        int l = (int)strlen(c->names[i]);
        if (l < len) len = l;
        for (int j = prefix_len; j < len; j++) {
            if (c->names[0][j] != c->names[i][j]) {
                len = j;
                break;
            }
        }
    }
    return len;
}

/**
 * 列出目录下匹配前缀的文件名
 */
static void complete_files(const char *dir, const char *prefix, int prefix_len,
                           struct completion *c) {
    int fd = vfs_opendir(dir);
    if (fd < 0) return;

    struct vfs_dirent ent;
    for (uint32_t idx = 0; vfs_readdir_index(fd, idx, &ent) == 0; idx++) {
        if (prefix_len == 0 || strncmp(ent.name, prefix, (size_t)prefix_len) == 0) {
            complete_add(c, ent.name);
        }
    }
    vfs_close(fd);
}

/* 在 main.c 中定义 */
extern const char *shell_get_builtin_name(int index);

/**
 * 补全命令名 (内建 + PATH 下的可执行文件)
 */
static void complete_command(const char *prefix, int prefix_len, struct completion *c) {
    /* 内建命令 */
    for (int i = 0; ; i++) {
        const char *name = shell_get_builtin_name(i);
        if (!name) break;
        if (strncmp(name, prefix, (size_t)prefix_len) == 0) {
            complete_add(c, name);
        }
    }

    /* PATH 目录下的文件 */
    for (int p = 0; p < path_count(); p++) {
        const char *dir = path_get(p);
        int         dfd = vfs_opendir(dir);
        if (dfd < 0) continue;

        struct vfs_dirent ent;
        for (uint32_t idx = 0; vfs_readdir_index(dfd, idx, &ent) == 0; idx++) {
            /* 去掉 .elf 后缀匹配 */
            char name[64];
            strncpy(name, ent.name, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            int nlen = (int)strlen(name);
            if (nlen > 4 && strcmp(name + nlen - 4, ".elf") == 0) {
                name[nlen - 4] = '\0';
            }

            if (strncmp(name, prefix, (size_t)prefix_len) == 0) {
                /* 检查是否已存在(避免重复) */
                int dup = 0;
                for (int j = 0; j < c->count; j++) {
                    if (strcmp(c->names[j], name) == 0) { dup = 1; break; }
                }
                if (!dup) complete_add(c, name);
            }
        }
        vfs_close(dfd);
    }
}

static void do_complete(const char *prompt) {
    /* 找到光标前的 token */
    int token_start = g_cursor;
    while (token_start > 0 && g_buf[token_start - 1] != ' ') {
        token_start--;
    }

    char token[LINE_MAX_LEN];
    int  token_len = g_cursor - token_start;
    memcpy(token, g_buf + token_start, (size_t)token_len);
    token[token_len] = '\0';

    /* 判断是命令补全还是文件补全 */
    int is_command = 1;
    for (int i = 0; i < token_start; i++) {
        if (g_buf[i] != ' ') { is_command = 0; break; }
    }

    struct completion comp;
    memset(&comp, 0, sizeof(comp));

    if (is_command && token[0] != '/' && token[0] != '.') {
        /* 补全命令名 */
        complete_command(token, token_len, &comp);
    } else {
        /* 补全文件路径 */
        char dir[LINE_MAX_LEN] = ".";
        const char *file_prefix = token;
        int file_prefix_len = token_len;

        /* 分离目录和文件前缀 */
        char *last_slash = NULL;
        for (int i = 0; i < token_len; i++) {
            if (token[i] == '/') last_slash = token + i;
        }

        if (last_slash) {
            int dir_len = (int)(last_slash - token);
            if (dir_len == 0) {
                dir[0] = '/'; dir[1] = '\0';
            } else {
                memcpy(dir, token, (size_t)dir_len);
                dir[dir_len] = '\0';
            }
            file_prefix = last_slash + 1;
            file_prefix_len = token_len - dir_len - 1;
        }

        complete_files(dir, file_prefix, file_prefix_len, &comp);
    }

    if (comp.count == 0) {
        out_char('\a'); /* 蜂鸣 */
        return;
    }

    if (comp.count == 1) {
        /* 唯一匹配: 补全 */
        const char *match = comp.names[0];
        int match_len = (int)strlen(match);
        /* 只需要从 file_prefix 后面插入 */
        const char *suffix;
        int suffix_len;

        if (is_command && token[0] != '/' && token[0] != '.') {
            suffix = match + token_len;
            suffix_len = match_len - token_len;
        } else {
            /* 文件补全: 只补全文件名部分 */
            char *last_slash = NULL;
            for (int i = 0; i < token_len; i++) {
                if (token[i] == '/') last_slash = token + i;
            }
            int file_prefix_len = last_slash ? (token_len - (int)(last_slash - token) - 1) : token_len;
            suffix = match + file_prefix_len;
            suffix_len = match_len - file_prefix_len;
        }

        for (int i = 0; i < suffix_len; i++) {
            insert_char(suffix[i], prompt);
        }
        /* 如果是目录不加空格，否则加空格 */
        if (is_command) {
            insert_char(' ', prompt);
        }
        return;
    }

    /* 多个匹配: 补全公共前缀 */
    const char *first = comp.names[0];
    int file_prefix_len = token_len;
    if (!is_command || token[0] == '/' || token[0] == '.') {
        char *last_slash = NULL;
        for (int i = 0; i < token_len; i++) {
            if (token[i] == '/') last_slash = token + i;
        }
        file_prefix_len = last_slash ? (token_len - (int)(last_slash - token) - 1) : token_len;
    }

    int common = common_prefix_len(&comp, file_prefix_len);
    if (common > file_prefix_len) {
        int extra;
        if (is_command && token[0] != '/' && token[0] != '.') {
            extra = common - token_len;
            for (int i = 0; i < extra; i++) {
                insert_char(first[token_len + i], prompt);
            }
        } else {
            extra = common - file_prefix_len;
            for (int i = 0; i < extra; i++) {
                insert_char(first[file_prefix_len + i], prompt);
            }
        }
    } else {
        /* 列出所有候选 */
        out_char('\n');
        for (int i = 0; i < comp.count; i++) {
            out_cstr(comp.names[i]);
            out_cstr("  ");
        }
        out_char('\n');
        redraw_line(prompt);
    }
}

/* ---- 历史文件 ---- */

void line_init(void) {
    g_history_count = 0;
    memset(g_history, 0, sizeof(g_history));

    /* 尝试加载历史文件 */
    int fd = vfs_open(g_history_file, VFS_O_RDONLY);
    if (fd < 0) return;

    char filebuf[4096];
    int  total = 0;
    int  n;
    while ((n = vfs_read(fd, filebuf + total, (int)sizeof(filebuf) - total - 1)) > 0) {
        total += n;
        if (total >= (int)sizeof(filebuf) - 1) break;
    }
    vfs_close(fd);

    filebuf[total] = '\0';

    /* 按行解析 */
    char *p = filebuf;
    while (*p && g_history_count < HISTORY_MAX) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        if (*p) {
            strncpy(g_history[g_history_count], p, LINE_MAX_LEN - 1);
            g_history[g_history_count][LINE_MAX_LEN - 1] = '\0';
            g_history_count++;
        }
        if (!nl) break;
        p = nl + 1;
    }
}

void line_add_history(const char *cmd) {
    if (!cmd || !*cmd) return;

    /* 不保存连续重复 */
    if (g_history_count > 0 && strcmp(g_history[g_history_count - 1], cmd) == 0) {
        return;
    }

    if (g_history_count >= HISTORY_MAX) {
        /* 移除最旧的 */
        memmove(g_history[0], g_history[1], sizeof(g_history[0]) * (HISTORY_MAX - 1));
        g_history_count = HISTORY_MAX - 1;
    }

    strncpy(g_history[g_history_count], cmd, LINE_MAX_LEN - 1);
    g_history[g_history_count][LINE_MAX_LEN - 1] = '\0';
    g_history_count++;
}

void line_save_history(void) {
    int fd = vfs_open(g_history_file, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return;

    for (int i = 0; i < g_history_count; i++) {
        int len = (int)strlen(g_history[i]);
        vfs_write(fd, g_history[i], len);
        vfs_write(fd, "\n", 1);
    }
    vfs_close(fd);
}

/* ---- 主入口 ---- */

char *line_read(char *buf, int size, const char *prompt) {
    /* 切换到 RAW 模式 */
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_RAW, 0);
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_ECHO, 0);

    /* 初始化编辑状态 */
    g_len = 0;
    g_cursor = 0;
    g_buf[0] = '\0';
    g_hist_pos = -1;
    g_saved[0] = '\0';
    g_prompt_len = (int)strlen(prompt);

    /* 输出 prompt */
    out_cstr(prompt);

    char *result = buf;

    for (;;) {
        char c;
        int n = (int)read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            msleep(10);
            continue;
        }

        if (c == '\r' || c == '\n') {
            out_char('\n');
            break;
        }

        if (c == 0x03) { /* Ctrl+C */
            out_cstr("^C\n");
            g_len = 0;
            g_cursor = 0;
            g_buf[0] = '\0';
            out_cstr(prompt);
            continue;
        }

        if (c == 0x04) { /* Ctrl+D */
            if (g_len == 0) {
                result = NULL;
                break;
            }
            continue; /* 非空行忽略 */
        }

        if (c == '\b' || c == 0x7F) {
            delete_char(prompt);
            continue;
        }

        if (c == '\t') {
            do_complete(prompt);
            continue;
        }

        if (c == 0x1B) { /* ESC - 转义序列 */
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) continue;
            if (seq[0] != '[') continue;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) continue;

            switch (seq[1]) {
            case 'A': history_up(prompt); break;
            case 'B': history_down(prompt); break;
            case 'C': move_right(); break;
            case 'D': move_left(); break;
            }
            continue;
        }

        if (c >= 32 && c < 127) {
            insert_char(c, prompt);
        }
    }

    /* 恢复 COOKED 模式 */
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_COOKED, 0);
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_ECHO, 1);

    if (result) {
        int copy_len = g_len;
        if (copy_len >= size) copy_len = size - 1;
        memcpy(buf, g_buf, (size_t)copy_len);
        buf[copy_len] = '\0';
    }

    return result;
}
