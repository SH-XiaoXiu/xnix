/**
 * @file main.c
 * @brief sudo: 以目标用户身份执行命令
 *
 * 通过 userd 认证并以 root 身份执行指定命令.
 * 向 session endpoint 发送 USER_OP_SUDO.
 *
 * Usage: sudo <command> [args...]
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/process.h>
#include <xnix/env.h>
#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/protocol/tty.h>
#include <xnix/protocol/user.h>
#include <xnix/syscall.h>

#define MAX_PASS 32

/**
 * RAW 模式读密码, 无回显
 */
static int read_password(char *buf, int maxlen) {
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_RAW, 0UL);
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_ECHO, 0UL);

    int pos = 0;
    while (pos < maxlen - 1) {
        char c;
        int  n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            msleep(50);
            continue;
        }
        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            break;
        }
        if (c == '\b' || c == 127) {
            if (pos > 0)
                pos--;
            continue;
        }
        if (c < 32)
            continue;
        buf[pos++] = c;
    }
    buf[pos] = '\0';

    ioctl(STDIN_FILENO, TTY_IOCTL_SET_COOKED, 0UL);
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_ECHO, 1UL);
    return pos;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: sudo <command> [args...]\n");
        return 1;
    }

    /* 查找 session endpoint */
    handle_t session_ep = env_get_handle("user_session");
    if (session_ep == HANDLE_INVALID) {
        printf("sudo: no user session\n");
        return 1;
    }

    /* 初始化 VFS 客户端 (解析命令路径) */
    handle_t vfs_ep = env_get_handle("vfs_ep");
    if (vfs_ep != HANDLE_INVALID)
        vfs_client_init(vfs_ep);

    /* 读取密码 (RAW 模式, 无回显) */
    write(STDOUT_FILENO, "[sudo] Password: ", 17);
    char password[MAX_PASS] = {0};
    read_password(password, sizeof(password));

    /* 构建 sudo 请求 */
    struct user_sudo_req sudo_req;
    memset(&sudo_req, 0, sizeof(sudo_req));
    strncpy(sudo_req.password, password, USER_PASS_MAX - 1);
    memset(password, 0, sizeof(password));

    int build_ret = posix_spawnp_make_exec_args(
        &sudo_req.exec_args, argv[1], argc - 1, (const char **)&argv[1]);
    if (build_ret < 0) {
        if (build_ret == -ENOENT)
            printf("sudo: %s: command not found\n", argv[1]);
        else
            printf("sudo: %s\n", strerror(-build_ret));
        return 1;
    }

    /* 发送 USER_OP_SUDO 到 session endpoint */
    struct ipc_message req = {0};
    req.regs.data[0] = USER_OP_SUDO;
    req.regs.data[1] = 0; /* target_uid = root */
    req.buffer.data  = (uint64_t)(uintptr_t)&sudo_req;
    req.buffer.size  = sizeof(sudo_req);

    struct ipc_message reply = {0};
    int ret = sys_ipc_call(session_ep, &req, &reply, 10000);
    if (ret < 0) {
        printf("sudo: request failed: %s\n", strerror(-ret));
        return 1;
    }

    int pid = (int32_t)reply.regs.data[1];
    if (pid < 0) {
        if (pid == -EACCES)
            printf("sudo: authentication failed\n");
        else
            printf("sudo: %s\n", strerror(-pid));
        return 1;
    }

    /* 等待子进程退出 */
    int status;
    waitpid(pid, &status, 0);
    return status;
}
