/**
 * @file svc_notify.c
 * @brief 服务通知实现
 */

#include <string.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

#define SVC_MSG_READY 0xF001

/**
 * 服务就绪通知消息
 */
struct svc_ready_msg {
    uint32_t magic;
    uint32_t pid;
    char     name[16];
};

/**
 * 通知 init 服务已就绪
 */
int svc_notify_ready(const char *name) {
    if (!name || name[0] == '\0') {
        return IPC_ERR_INVALID;
    }

    /* 查找 init_notify endpoint */
    int init_ep = sys_handle_find("init_notify");
    if (init_ep < 0) {
        return IPC_ERR_INVALID;
    }

    struct ipc_message msg = {0};
    msg.regs.data[0]       = SVC_MSG_READY;
    msg.regs.data[1]       = (uint32_t)sys_getpid();
    {
        char   name_buf[16] = {0};
        size_t name_len     = strlen(name);
        if (name_len > 15) {
            name_len = 15;
        }
        memcpy(name_buf, name, name_len);
        memcpy(&msg.regs.data[2], name_buf, sizeof(name_buf));
    }

    return sys_ipc_send((uint32_t)init_ep, &msg, 1000);
}
