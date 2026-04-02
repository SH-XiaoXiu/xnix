/**
 * @file svc_notify.c
 * @brief 服务通知实现
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/abi/handle.h>
#include <xnix/ipc.h>
#include <xnix/protocol/svc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

/* 重试参数 */
#define SVC_NOTIFY_MAX_RETRIES   5
#define SVC_NOTIFY_INITIAL_MS  200
#define SVC_NOTIFY_SEND_TIMEOUT 2000

/**
 * 通知 init 服务已就绪
 *
 * 带重试: 如果 sys_ipc_send 失败(权限/超时),最多重试
 * SVC_NOTIFY_MAX_RETRIES 次,每次间隔递增.
 */
int svc_notify_ready(const char *name) {
    if (!name || name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    /* 查找 init_notify endpoint */
    handle_t init_ep = sys_handle_find("init_notify");
    if (init_ep == HANDLE_INVALID) {
        printf("[svc] %s: init_notify handle not found\n", name);
        return -1; /* errno 已由系统调用包装器设置 */
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

    uint32_t delay_ms = SVC_NOTIFY_INITIAL_MS;
    for (int attempt = 0; attempt <= SVC_NOTIFY_MAX_RETRIES; attempt++) {
        int ret = sys_ipc_send((uint32_t)init_ep, &msg, SVC_NOTIFY_SEND_TIMEOUT);
        if (ret == 0) {
            return 0;
        }

        printf("[svc] %s: notify_ready failed (attempt %d/%d, err=%d)\n",
               name, attempt + 1, SVC_NOTIFY_MAX_RETRIES + 1, ret);

        if (attempt < SVC_NOTIFY_MAX_RETRIES) {
            msleep(delay_ms);
            delay_ms *= 2;
            if (delay_ms > 2000) {
                delay_ms = 2000;
            }
        }
    }

    printf("[svc] %s: notify_ready FAILED after all retries\n", name);
    return -1;
}
