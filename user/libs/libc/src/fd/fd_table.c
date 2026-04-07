/**
 * @file fd_table.c
 * @brief 统一文件描述符表实现
 *
 * fd 0/1/2 在初始化时自动绑定到 stdin/stdout/stderr handle.
 * 不关心对端是 TTY/syslog/其他 — 统一走 IO 协议.
 */

#include <string.h>
#include <unistd.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/fd.h>
#include <xnix/syscall.h>

static struct fd_entry g_fd_table[FD_MAX];

/**
 * 查询 handle 类型, 判断是否为 pipe handle
 */
static uint8_t detect_pipe_flags(handle_t h, uint8_t base_flags) {
    if (h == HANDLE_INVALID) return base_flags;

    struct abi_handle_info info;
    int count = sys_handle_list(&info, 1);
    /* 用 handle_list 太重; 改用遍历查找 */
    struct abi_handle_info buf[16];
    count = sys_handle_list(buf, 16);
    for (int i = 0; i < count; i++) {
        if (buf[i].handle == h) {
            if (buf[i].type == HANDLE_PIPE_READ || buf[i].type == HANDLE_PIPE_WRITE) {
                return base_flags | FD_FLAG_PIPE;
            }
            break;
        }
    }
    return base_flags;
}

void fd_table_init(void) {
    memset(g_fd_table, 0, sizeof(g_fd_table));

    handle_t in  = env_get_handle(HANDLE_STDIO_STDIN);
    handle_t out = env_get_handle(HANDLE_STDIO_STDOUT);
    handle_t err = env_get_handle(HANDLE_STDIO_STDERR);

    uint8_t in_flags  = detect_pipe_flags(in, FD_FLAG_READ);
    uint8_t out_flags = detect_pipe_flags(out, FD_FLAG_WRITE);

    /* 不做 fallback: 没有 stdio handle 时 fd 0/1/2 不安装,
     * stdio 层会自动使用 DEBUG 通道 (SYS_DEBUG_WRITE). */

    if (in  != HANDLE_INVALID) fd_install(0, in,  0, 0, in_flags);
    if (out != HANDLE_INVALID) fd_install(1, out, 0, 0, out_flags);
    if (err != HANDLE_INVALID) fd_install(2, err, 0, 0, FD_FLAG_WRITE);
}

void fd_table_fini(void) {
    for (int fd = 0; fd < FD_MAX; fd++) {
        if (fd_get(fd) != NULL) {
            close(fd);
        }
    }
}

int fd_alloc(void) {
    for (int i = 0; i < FD_MAX; i++) {
        if (g_fd_table[i].handle == 0 && g_fd_table[i].flags == 0) {
            return i;
        }
    }
    return -1;
}

int fd_alloc_at(int fd) {
    if (fd < 0 || fd >= FD_MAX) {
        return -1;
    }
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
    if (g_fd_table[fd].handle == 0 && g_fd_table[fd].flags == 0) {
        return NULL;
    }
    return &g_fd_table[fd];
}

handle_t fd_get_handle(int fd) {
    if (fd < 0 || fd >= FD_MAX) {
        return HANDLE_INVALID;
    }
    if (g_fd_table[fd].handle == 0 && g_fd_table[fd].flags == 0) {
        return HANDLE_INVALID;
    }
    return g_fd_table[fd].handle;
}

struct fd_entry *fd_install(int fd, handle_t handle, uint32_t session, uint32_t offset,
                            uint8_t flags) {
    if (fd < 0 || fd >= FD_MAX) {
        return NULL;
    }
    memset(&g_fd_table[fd], 0, sizeof(g_fd_table[fd]));
    g_fd_table[fd].handle  = handle;
    g_fd_table[fd].session = session;
    g_fd_table[fd].offset  = offset;
    g_fd_table[fd].flags   = flags;
    return &g_fd_table[fd];
}
