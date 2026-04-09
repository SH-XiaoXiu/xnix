/**
 * @file main.c
 * @brief login: 用户登录程序
 *
 * 由 init 在每个 TTY 上启动, 替代直接启动 shell.
 * 认证成功后以受控句柄集启动用户 shell.
 *
 * 流程:
 *   1. 显示 login 提示
 *   2. 读取用户名 / 密码
 *   3. 通过 userd 认证 (USER_OP_LOGIN)
 *   4. 启动用户 shell (显式句柄, 不用 proc_inherit_named)
 *   5. 等待 shell 退出
 *   6. 发送 USER_OP_LOGOUT
 *   7. 循环
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/proc.h>
#include <xnix/protocol/tty.h>
#include <xnix/protocol/user.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

#define MAX_INPUT 64

static handle_t g_user_ep;
static handle_t g_vfs_ep;

/**
 * RAW 模式读一行, 手动回显
 *
 * @param echo true=回显字符, false=静默(密码)
 */
static int read_line(char *buf, int maxlen, int echo) {
    /* 切 RAW + 关终端 echo (由我们手动回显) */
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
            if (pos > 0) {
                pos--;
                if (echo)
                    write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }
        /* 忽略控制字符 */
        if (c < 32)
            continue;
        buf[pos++] = c;
        if (echo)
            write(STDOUT_FILENO, &c, 1);
    }
    buf[pos] = '\0';

    /* 恢复 cooked + echo */
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_COOKED, 0UL);
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_ECHO, 1UL);

    return pos;
}

/**
 * 向 userd 发起登录请求
 *
 * @param session_ep  输出: session endpoint handle
 * @param info        输出: 用户信息
 * @return 0 成功, <0 错误
 */
static int do_login(const char *username, const char *password,
                    handle_t *session_ep, struct user_info *info) {
    struct user_login_req login_req;
    memset(&login_req, 0, sizeof(login_req));
    strncpy(login_req.username, username, USER_NAME_MAX - 1);
    strncpy(login_req.password, password, USER_PASS_MAX - 1);

    struct ipc_message req = {0};
    req.regs.data[0] = USER_OP_LOGIN;
    req.buffer.data  = (uint64_t)(uintptr_t)&login_req;
    req.buffer.size  = sizeof(login_req);

    struct ipc_message reply = {0};
    char               reply_buf[sizeof(struct user_info)];
    reply.buffer.data = (uint64_t)(uintptr_t)reply_buf;
    reply.buffer.size = sizeof(reply_buf);

    int ret = sys_ipc_call(g_user_ep, &req, &reply, 5000);
    if (ret < 0)
        return ret;

    int result = (int32_t)reply.regs.data[1];
    if (result < 0)
        return result;

    /* 提取 session handle */
    if (reply.handles.count > 0)
        *session_ep = reply.handles.handles[0];
    else
        return -EPROTO;

    /* 提取 user_info */
    if (reply.buffer.size >= sizeof(*info))
        memcpy(info, reply_buf, sizeof(*info));

    return 0;
}

/**
 * 向 session endpoint 发送 LOGOUT
 */
static void do_logout(handle_t session_ep) {
    struct ipc_message req = {0};
    req.regs.data[0] = USER_OP_LOGOUT;

    struct ipc_message reply = {0};
    sys_ipc_call(session_ep, &req, &reply, 2000);
}

/**
 * 启动用户 shell
 *
 * 使用显式句柄集, 不用 proc_inherit_named.
 */
static int spawn_shell(handle_t session_ep, const struct user_info *info) {
    const char *shell_path = info->shell;
    if (shell_path[0] == '\0')
        shell_path = "/bin/shell.elf";

    struct proc_builder b;
    proc_init(&b, shell_path);

    /* stdio: 继承 login 的 stdin/stdout/stderr */
    proc_add_handle(&b, fd_get_handle(STDIN_FILENO), HANDLE_STDIO_STDIN);
    proc_add_handle(&b, fd_get_handle(STDOUT_FILENO), HANDLE_STDIO_STDOUT);
    proc_add_handle(&b, fd_get_handle(STDERR_FILENO), HANDLE_STDIO_STDERR);

    /* 用户会话 */
    proc_add_handle(&b, session_ep, "user_session");

    /* 系统服务 */
    if (g_vfs_ep != HANDLE_INVALID)
        proc_add_handle(&b, g_vfs_ep, "vfs_ep");
    if (g_user_ep != HANDLE_INVALID)
        proc_add_handle(&b, g_user_ep, "user_ep");

    /* 可选服务: console, ws */
    handle_t h;
    h = env_get_handle("console_ep");
    if (h != HANDLE_INVALID)
        proc_add_handle(&b, h, "console_ep");
    h = env_get_handle("ws_ep");
    if (h != HANDLE_INVALID)
        proc_add_handle(&b, h, "ws_ep");

    /* 不传 init_notify: shell 由 login 管理, 不需要通知 init */

    int pid = proc_spawn(&b);
    return pid;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 解析 --svc= 参数 */
    const char *svc_name = "login";
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "--svc=", 6) == 0)
            svc_name = argv[i] + 6;
    }

    /* 获取必要句柄 */
    g_user_ep = env_require("user_ep");
    if (g_user_ep == HANDLE_INVALID) {
        printf("[login] FATAL: user_ep not found\n");
        return 1;
    }

    g_vfs_ep = env_get_handle("vfs_ep");
    if (g_vfs_ep != HANDLE_INVALID)
        vfs_client_init(g_vfs_ep);

    /* 通知 init 就绪 */
    svc_notify_ready(svc_name);

    /* 登录循环 */
    while (1) {
        ioctl(STDIN_FILENO, TTY_IOCTL_FLUSH_INPUT, 0UL);

        write(STDOUT_FILENO, "\nXnix login: ", 13);
        char username[MAX_INPUT];
        read_line(username, sizeof(username), 1); /* echo ON */

        if (username[0] == '\0')
            continue;

        /* 读取密码 (无回显) */
        write(STDOUT_FILENO, "Password: ", 10);
        char password[MAX_INPUT];
        read_line(password, sizeof(password), 0); /* echo OFF */

        /* 认证 */
        handle_t         session_ep = HANDLE_INVALID;
        struct user_info info;
        int              ret = do_login(username, password, &session_ep, &info);

        /* 清除密码 */
        memset(password, 0, sizeof(password));

        if (ret < 0) {
            printf("Login incorrect\n");
            msleep(1000);
            continue;
        }

        printf("\nWelcome, %s\n", info.name);

        /* 刷新输入缓冲 */
        ioctl(STDIN_FILENO, TTY_IOCTL_FLUSH_INPUT, 0UL);

        /* 启动 shell */
        int pid = spawn_shell(session_ep, &info);
        if (pid < 0) {
            printf("[login] failed to start shell: %s\n", strerror(-pid));
            do_logout(session_ep);
            continue;
        }

        /* 等待 shell 退出 */
        int status;
        waitpid(pid, &status, 0);

        /* 注销 */
        do_logout(session_ep);
    }

    return 0;
}
