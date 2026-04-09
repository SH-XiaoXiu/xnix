/**
 * @file main.c
 * @brief useradd - 创建新用户
 *
 * Usage: useradd <username> [password]
 *
 * 通过 userd 创建新用户, 自动分配 UID 并创建 /home/<username>.
 * 需要 root 权限 (通过 session handle 验证 uid=0).
 */

#include <stdio.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/protocol/user.h>
#include <xnix/syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: useradd <username> [password]\n");
        return 1;
    }

    handle_t user_ep = env_get_handle("user_ep");
    if (user_ep == HANDLE_INVALID) {
        printf("useradd: user_ep not found\n");
        return 1;
    }

    handle_t session_ep = env_get_handle("user_session");
    if (session_ep == HANDLE_INVALID) {
        printf("useradd: not logged in\n");
        return 1;
    }

    struct user_adduser_req add_req;
    memset(&add_req, 0, sizeof(add_req));
    strncpy(add_req.username, argv[1], USER_NAME_MAX - 1);
    if (argc >= 3)
        strncpy(add_req.password, argv[2], USER_PASS_MAX - 1);

    struct ipc_message req = {0};
    req.regs.data[0]       = USER_OP_ADDUSER;
    req.handles.handles[0] = session_ep; /* 身份验证 */
    req.handles.count      = 1;
    req.buffer.data        = (uint64_t)(uintptr_t)&add_req;
    req.buffer.size        = sizeof(add_req);

    struct ipc_message reply = {0};
    int ret = sys_ipc_call(user_ep, &req, &reply, 5000);
    if (ret < 0) {
        printf("useradd: request failed\n");
        return 1;
    }

    int result = (int32_t)reply.regs.data[1];
    if (result < 0) {
        if (result == -17) /* EEXIST */
            printf("useradd: user '%s' already exists\n", argv[1]);
        else if (result == -1) /* EPERM */
            printf("useradd: permission denied\n");
        else
            printf("useradd: failed (%d)\n", result);
        return 1;
    }

    uint32_t uid = reply.regs.data[2];
    printf("User '%s' created (uid=%u, home=/home/%s)\n", argv[1], uid, argv[1]);
    return 0;
}
