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
    char     name[16];
};

/**
 * 通知 init 服务已就绪
 */
int svc_notify_ready(const char *name) {
    /* 查找 init_notify endpoint */
    int init_ep = sys_handle_find("init_notify");
    if (init_ep < 0) {
        return -1;
    }

    struct svc_ready_msg msg_data = {
        .magic = SVC_MSG_READY,
    };

    /* 复制服务名 */
    size_t name_len = strlen(name);
    if (name_len > 15) {
        name_len = 15;
    }
    memcpy(msg_data.name, name, name_len);
    msg_data.name[name_len] = '\0';

    struct ipc_message msg = {0};
    msg.regs.data[0]       = SVC_MSG_READY;
    msg.buffer.data        = &msg_data;
    msg.buffer.size        = sizeof(msg_data);

    /* 异步发送(timeout=0 非阻塞) */
    return sys_ipc_send((uint32_t)init_ep, &msg, 0);
}
