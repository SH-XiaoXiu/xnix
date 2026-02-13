/**
 * @file fd_table.c
 * @brief 统一文件描述符表实现
 *
 * 管理 int fd -> { handle, type, state } 的映射.
 * fd 0/1/2 在初始化时自动绑定到 stdin/stdout/stderr handle.
 */

#include <string.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/fd.h>

static struct fd_entry g_fd_table[FD_MAX];

/* 查找 tty endpoint (优先 tty1/serial,保证服务默认 stdio 走串口) */
static handle_t find_tty_ep(void) {
    handle_t h;

    h = env_get_handle("tty1");
    if (h != HANDLE_INVALID) {
        return h;
    }

    h = env_get_handle("tty0");
    if (h != HANDLE_INVALID) {
        return h;
    }

    return HANDLE_INVALID;
}

/* 查找标准流 handle */
static handle_t find_stdio_ep(const char *stdio_name) {
    handle_t h = env_get_handle(stdio_name);
    if (h != HANDLE_INVALID) {
        return h;
    }
    return HANDLE_INVALID;
}

void fd_table_init(void) {
    memset(g_fd_table, 0, sizeof(g_fd_table));

    /* 查找标准 handle (stdin/stdout/stderr) */
    handle_t stdin_ep  = find_stdio_ep(HANDLE_STDIO_STDIN);
    handle_t stdout_ep = find_stdio_ep(HANDLE_STDIO_STDOUT);
    handle_t stderr_ep = find_stdio_ep(HANDLE_STDIO_STDERR);

    /* 回退到 tty handle */
    if (stdin_ep == HANDLE_INVALID || stdout_ep == HANDLE_INVALID || stderr_ep == HANDLE_INVALID) {
        handle_t tty = find_tty_ep();
        if (stdin_ep == HANDLE_INVALID) {
            stdin_ep = tty;
        }
        if (stdout_ep == HANDLE_INVALID) {
            stdout_ep = tty;
        }
        if (stderr_ep == HANDLE_INVALID) {
            stderr_ep = tty;
        }
    }

    /* fd 0 = stdin */
    g_fd_table[0].handle = stdin_ep;
    g_fd_table[0].type   = (stdin_ep != HANDLE_INVALID) ? FD_TYPE_TTY : FD_TYPE_NONE;
    g_fd_table[0].flags  = FD_FLAG_READ;

    /* fd 1 = stdout */
    g_fd_table[1].handle = stdout_ep;
    g_fd_table[1].type   = (stdout_ep != HANDLE_INVALID) ? FD_TYPE_TTY : FD_TYPE_NONE;
    g_fd_table[1].flags  = FD_FLAG_WRITE;

    /* fd 2 = stderr */
    g_fd_table[2].handle = stderr_ep;
    g_fd_table[2].type   = (stderr_ep != HANDLE_INVALID) ? FD_TYPE_TTY : FD_TYPE_NONE;
    g_fd_table[2].flags  = FD_FLAG_WRITE;
}

int fd_alloc(void) {
    for (int i = 0; i < FD_MAX; i++) {
        if (g_fd_table[i].type == FD_TYPE_NONE) {
            return i;
        }
    }
    return -1;
}

int fd_alloc_at(int fd) {
    if (fd < 0 || fd >= FD_MAX) {
        return -1;
    }
    /* 如果已占用,仅清零(调用者负责先 close) */
    memset(&g_fd_table[fd], 0, sizeof(g_fd_table[fd]));
    return 0;
}

void fd_free(int fd) {
    if (fd < 0 || fd >= FD_MAX) {
        return;
    }
    memset(&g_fd_table[fd], 0, sizeof(g_fd_table[fd]));
}

struct fd_entry *fd_get(int fd) {
    if (fd < 0 || fd >= FD_MAX) {
        return NULL;
    }
    if (g_fd_table[fd].type == FD_TYPE_NONE) {
        return NULL;
    }
    return &g_fd_table[fd];
}

handle_t fd_get_handle(int fd) {
    if (fd < 0 || fd >= FD_MAX) {
        return HANDLE_INVALID;
    }
    if (g_fd_table[fd].type == FD_TYPE_NONE) {
        return HANDLE_INVALID;
    }
    return g_fd_table[fd].handle;
}

struct fd_entry *fd_install(int fd, handle_t handle, uint8_t type, uint8_t flags) {
    if (fd < 0 || fd >= FD_MAX) {
        return NULL;
    }
    memset(&g_fd_table[fd], 0, sizeof(g_fd_table[fd]));
    g_fd_table[fd].handle = handle;
    g_fd_table[fd].type   = type;
    g_fd_table[fd].flags  = flags;
    return &g_fd_table[fd];
}
