/**
 * @file server.c
 * @brief UDM Server 框架实现
 */

#include <d/server.h>
#include <string.h>
#include <xnix/syscall.h>

/* 接收消息的缓冲区 */
#define UDM_RECV_BUF_SIZE 4096
static char g_recv_buf[UDM_RECV_BUF_SIZE];

void udm_server_init(struct udm_server *srv) {
    /* 目前无需初始化 */
    (void)srv;
}

void udm_server_run(struct udm_server *srv) {
    struct ipc_message msg;

    while (1) {
        memset(&msg, 0, sizeof(msg));
        msg.buffer.data = (uint64_t)(uintptr_t)g_recv_buf;
        msg.buffer.size = UDM_RECV_BUF_SIZE;

        if (sys_ipc_receive(srv->endpoint, &msg, 0) < 0) {
            continue;
        }

        int ret = srv->handler(&msg);
        /* handler 返回 0: 立即回复; 返回 1: 延迟回复(由 handler 自行调用 reply_to) */
        if (ret == 0) {
            sys_ipc_reply(&msg);
        }
    }
}
