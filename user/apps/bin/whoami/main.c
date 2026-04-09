/**
 * @file main.c
 * @brief whoami - 显示当前用户名
 *
 * 通过 session endpoint 向 userd 查询当前用户信息.
 */

#include <stdio.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/protocol/user.h>
#include <xnix/syscall.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    handle_t session_ep = env_get_handle("user_session");
    if (session_ep == HANDLE_INVALID) {
        printf("whoami: no user session\n");
        return 1;
    }

    struct ipc_message req = {0};
    req.regs.data[0] = USER_OP_WHOAMI;

    struct ipc_message reply = {0};
    struct user_info   info;
    reply.buffer.data = (uint64_t)(uintptr_t)&info;
    reply.buffer.size = sizeof(info);

    int ret = sys_ipc_call(session_ep, &req, &reply, 2000);
    if (ret < 0 || (int32_t)reply.regs.data[1] < 0) {
        printf("whoami: failed\n");
        return 1;
    }

    printf("%s\n", info.name);
    return 0;
}
