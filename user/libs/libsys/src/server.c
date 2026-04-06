/**
 * @file server.c
 * @brief 通用用户态服务循环辅助实现
 */

#include <string.h>
#include <xnix/sys/server.h>
#include <xnix/syscall.h>

#define SYS_RECV_BUF_SIZE 4096
static char g_recv_buf[SYS_RECV_BUF_SIZE];

void sys_server_init(struct sys_server *srv) {
    (void)srv;
}

void sys_server_run(struct sys_server *srv) {
    struct ipc_message msg;

    while (1) {
        memset(&msg, 0, sizeof(msg));
        msg.buffer.data = (uint64_t)(uintptr_t)g_recv_buf;
        msg.buffer.size = SYS_RECV_BUF_SIZE;

        if (sys_ipc_receive(srv->endpoint, &msg, 0) < 0) {
            continue;
        }

        int ret = srv->handler(&msg);
        if (ret == 0 && (msg.flags & ABI_IPC_FLAG_NOREPLY) == 0) {
            sys_ipc_reply(&msg);
        }
    }
}
