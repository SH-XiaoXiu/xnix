/**
 * @file main.c
 * @brief passwd - 修改当前用户密码
 *
 * 通过 session endpoint 向 userd 发送 USER_OP_PASSWD.
 * 需要验证旧密码.
 */

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/protocol/tty.h>
#include <xnix/protocol/user.h>
#include <xnix/syscall.h>

#define MAX_PASS 32

static int read_password(const char *prompt, char *buf, int maxlen) {
    write(STDOUT_FILENO, prompt, strlen(prompt));

    ioctl(STDIN_FILENO, TTY_IOCTL_SET_RAW, 0UL);
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_ECHO, 0UL);

    int pos = 0;
    while (pos < maxlen - 1) {
        char c;
        int  n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) { msleep(50); continue; }
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 127) { if (pos > 0) pos--; continue; }
        if (c < 32) continue;
        buf[pos++] = c;
    }
    buf[pos] = '\0';

    ioctl(STDIN_FILENO, TTY_IOCTL_SET_COOKED, 0UL);
    ioctl(STDIN_FILENO, TTY_IOCTL_SET_ECHO, 1UL);
    write(STDOUT_FILENO, "\n", 1);
    return pos;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    handle_t session_ep = env_get_handle("user_session");
    if (session_ep == HANDLE_INVALID) {
        printf("passwd: no user session\n");
        return 1;
    }

    char old_pass[MAX_PASS] = {0};
    char new_pass[MAX_PASS] = {0};
    char confirm[MAX_PASS]  = {0};

    read_password("Current password: ", old_pass, sizeof(old_pass));
    read_password("New password: ", new_pass, sizeof(new_pass));
    read_password("Confirm new password: ", confirm, sizeof(confirm));

    if (strcmp(new_pass, confirm) != 0) {
        printf("passwd: passwords do not match\n");
        return 1;
    }

    struct user_passwd_req passwd_req;
    memset(&passwd_req, 0, sizeof(passwd_req));
    strncpy(passwd_req.old_password, old_pass, USER_PASS_MAX - 1);
    strncpy(passwd_req.new_password, new_pass, USER_PASS_MAX - 1);

    memset(old_pass, 0, sizeof(old_pass));
    memset(new_pass, 0, sizeof(new_pass));
    memset(confirm, 0, sizeof(confirm));

    struct ipc_message req = {0};
    req.regs.data[0] = USER_OP_PASSWD;
    req.buffer.data  = (uint64_t)(uintptr_t)&passwd_req;
    req.buffer.size  = sizeof(passwd_req);

    struct ipc_message reply = {0};
    int ret = sys_ipc_call(session_ep, &req, &reply, 5000);

    memset(&passwd_req, 0, sizeof(passwd_req));

    if (ret < 0) {
        printf("passwd: request failed\n");
        return 1;
    }

    int result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        if (result == -13) /* EACCES */
            printf("passwd: authentication failed\n");
        else
            printf("passwd: failed (%d)\n", result);
        return 1;
    }

    printf("Password changed successfully\n");
    return 0;
}
