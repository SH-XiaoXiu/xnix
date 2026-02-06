/**
 * @file kernel/sys/sys_ipc.c
 * @brief IPC 相关系统调用
 */

#include <ipc/endpoint.h>
#include <ipc/notification.h>
#include <sys/syscall.h>
#include <xnix/config.h>
#include <xnix/errno.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/string.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>

/**
 * 将用户态 IPC 消息复制到内核缓冲区
 */
static int ipc_msg_copy_in(struct ipc_message **out_kmsg, struct ipc_message *user_msg,
                           bool copy_buffer) {
    if (!out_kmsg || !user_msg) {
        return -EINVAL;
    }

    struct ipc_message umsg;
    int                ret = copy_from_user(&umsg, user_msg, sizeof(umsg));
    if (ret < 0) {
        return ret;
    }

    if (copy_buffer) {
        if (umsg.buffer.size > CFG_IPC_MAX_BUF) {
            return -E2BIG;
        }
        if (umsg.buffer.size && !umsg.buffer.data) {
            return -EINVAL;
        }
    }

    struct ipc_message *kmsg = kzalloc(sizeof(*kmsg));
    if (!kmsg) {
        return -ENOMEM;
    }
    memcpy(kmsg, &umsg, sizeof(*kmsg));

    /* handles 已从用户态拷贝,将在 ipc_copy_msg 中处理 */

    if (copy_buffer && umsg.buffer.data && umsg.buffer.size) {
        void *kbuf = kmalloc(umsg.buffer.size);
        if (!kbuf) {
            kfree(kmsg);
            return -ENOMEM;
        }
        ret = copy_from_user(kbuf, umsg.buffer.data, umsg.buffer.size);
        if (ret < 0) {
            kfree(kbuf);
            kfree(kmsg);
            return ret;
        }
        kmsg->buffer.data = kbuf;
        kmsg->buffer.size = umsg.buffer.size;
    } else {
        kmsg->buffer.data = NULL;
        kmsg->buffer.size = 0;
    }

    *out_kmsg = kmsg;
    return 0;
}

/**
 * 将内核 IPC 消息复制回用户态
 */
static int ipc_msg_copy_out(struct ipc_message *user_msg, const struct ipc_message *kmsg,
                            void *user_buf_ptr, size_t user_buf_size) {
    if (!user_msg || !kmsg) {
        return -EINVAL;
    }

    if (user_buf_ptr && user_buf_size) {
        size_t n = kmsg->buffer.size;
        if (n > user_buf_size) {
            n = user_buf_size;
        }
        if (n) {
            int ret = copy_to_user(user_buf_ptr, kmsg->buffer.data, n);
            if (ret < 0) {
                return ret;
            }
        }
    }

    struct ipc_message out;
    memset(&out, 0, sizeof(out));
    memcpy(&out.regs, &kmsg->regs, sizeof(out.regs));
    out.buffer.data = user_buf_ptr;
    out.buffer.size = kmsg->buffer.size;
    memcpy(&out.handles, &kmsg->handles, sizeof(out.handles)); /* 拷贝传递的 handles */
    out.flags      = kmsg->flags;
    out.sender_tid = kmsg->sender_tid; /* 拷贝发送者 TID */

    return copy_to_user(user_msg, &out, sizeof(out));
}

/**
 * 释放内核 IPC 消息缓冲区
 */
static void ipc_msg_free(struct ipc_message *kmsg) {
    if (!kmsg) {
        return;
    }
    if (kmsg->buffer.data) {
        kfree(kmsg->buffer.data);
    }
    kfree(kmsg);
}

/**
 * 分配接收消息的内核缓冲区
 */
static int ipc_msg_alloc_recv(struct ipc_message **out_kmsg, struct ipc_message *user_msg,
                              void **out_user_buf, size_t *out_user_buf_size) {
    struct ipc_message umsg;
    int                ret = copy_from_user(&umsg, user_msg, sizeof(umsg));
    if (ret < 0) {
        return ret;
    }

    void  *user_buf_ptr  = umsg.buffer.data;
    size_t user_buf_size = umsg.buffer.size;

    if (user_buf_size > CFG_IPC_MAX_BUF) {
        return -E2BIG;
    }
    if (user_buf_size && !user_buf_ptr) {
        return -EINVAL;
    }

    struct ipc_message *kmsg = kzalloc(sizeof(*kmsg));
    if (!kmsg) {
        return -ENOMEM;
    }

    memcpy(&kmsg->regs, &umsg.regs, sizeof(kmsg->regs));
    kmsg->flags         = umsg.flags;
    kmsg->handles.count = 0;

    if (user_buf_ptr && user_buf_size) {
        kmsg->buffer.data = kmalloc(user_buf_size);
        if (!kmsg->buffer.data) {
            kfree(kmsg);
            return -ENOMEM;
        }
        kmsg->buffer.size = user_buf_size;
    } else {
        kmsg->buffer.data = NULL;
        kmsg->buffer.size = 0;
    }

    *out_kmsg          = kmsg;
    *out_user_buf      = user_buf_ptr;
    *out_user_buf_size = user_buf_size;
    return 0;
}

/* SYS_ENDPOINT_CREATE: ebx=name (可选) */
static int32_t sys_endpoint_create(const uint32_t *args) {
    const char *user_name = (const char *)(uintptr_t)args[0];
    char        kname[32] = {0};

    struct process *proc = process_current();
    if (!perm_check_name(proc, PERM_NODE_IPC_ENDPOINT_CREATE)) {
        return -EPERM;
    }

    /* 从用户空间复制名称(可选) */
    if (user_name) {
        int ret = copy_from_user(kname, user_name, sizeof(kname) - 1);
        if (ret < 0) {
            return ret;
        }
        kname[sizeof(kname) - 1] = '\0';
    }

    handle_t h = endpoint_create(kname[0] ? kname : NULL);
    if (h == HANDLE_INVALID) {
        return -ENOMEM;
    }
    return (int32_t)h;
}

/* SYS_IPC_SEND: ebx=ep, ecx=msg, edx=timeout */
static int32_t sys_ipc_send(const uint32_t *args) {
    handle_t            ep       = (handle_t)args[0];
    struct ipc_message *user_msg = (struct ipc_message *)(uintptr_t)args[1];
    uint32_t            timeout  = args[2];

    struct process *proc = process_current();
    if (!perm_check_name(proc, PERM_NODE_IPC_SEND)) {
        return -EPERM;
    }

    struct ipc_message *kmsg = NULL;
    int                 ret  = ipc_msg_copy_in(&kmsg, user_msg, true);
    if (ret < 0) {
        return ret;
    }

    ret = ipc_send(ep, kmsg, timeout);
    ipc_msg_free(kmsg);
    return ret;
}

static int32_t sys_ipc_send_async(const uint32_t *args) {
    handle_t            ep       = (handle_t)args[0];
    struct ipc_message *user_msg = (struct ipc_message *)(uintptr_t)args[1];

    struct process *proc = process_current();
    if (!perm_check_name(proc, PERM_NODE_IPC_SEND)) {
        return -EPERM;
    }

    struct ipc_message *kmsg = NULL;
    int                 ret  = ipc_msg_copy_in(&kmsg, user_msg, false);
    if (ret < 0) {
        return ret;
    }

    ret = ipc_send_async(ep, kmsg);
    ipc_msg_free(kmsg);
    return ret;
}

/* SYS_IPC_RECV: ebx=ep, ecx=msg, edx=timeout */
static int32_t sys_ipc_recv(const uint32_t *args) {
    handle_t            ep       = (handle_t)args[0];
    struct ipc_message *user_msg = (struct ipc_message *)(uintptr_t)args[1];
    uint32_t            timeout  = args[2];

    struct process *proc = process_current();
    if (!perm_check_name(proc, PERM_NODE_IPC_RECV)) {
        return -EPERM;
    }

    void               *user_buf_ptr  = NULL;
    size_t              user_buf_size = 0;
    struct ipc_message *kmsg          = NULL;

    int ret = ipc_msg_alloc_recv(&kmsg, user_msg, &user_buf_ptr, &user_buf_size);
    if (ret < 0) {
        return ret;
    }

    ret = ipc_receive(ep, kmsg, timeout);
    if (ret == 0) {
        ret = ipc_msg_copy_out(user_msg, kmsg, user_buf_ptr, user_buf_size);
    }
    ipc_msg_free(kmsg);
    return ret;
}

/* SYS_IPC_CALL: ebx=ep, ecx=req, edx=reply, esi=timeout */
static int32_t sys_ipc_call(const uint32_t *args) {
    handle_t            ep         = (handle_t)args[0];
    struct ipc_message *user_req   = (struct ipc_message *)(uintptr_t)args[1];
    struct ipc_message *user_reply = (struct ipc_message *)(uintptr_t)args[2];
    uint32_t            timeout    = args[3];

    struct process *proc = process_current();
    if (!perm_check_name(proc, PERM_NODE_IPC_SEND)) {
        return -EPERM;
    }

    struct ipc_message *kreq = NULL;
    int                 ret  = ipc_msg_copy_in(&kreq, user_req, true);
    if (ret < 0) {
        return ret;
    }

    void               *user_buf_ptr  = NULL;
    size_t              user_buf_size = 0;
    struct ipc_message *kreply        = NULL;

    ret = ipc_msg_alloc_recv(&kreply, user_reply, &user_buf_ptr, &user_buf_size);
    if (ret < 0) {
        ipc_msg_free(kreq);
        return ret;
    }

    ret = ipc_call(ep, kreq, kreply, timeout);
    if (ret == 0) {
        ret = ipc_msg_copy_out(user_reply, kreply, user_buf_ptr, user_buf_size);
    }

    ipc_msg_free(kreq);
    ipc_msg_free(kreply);
    return ret;
}

/* SYS_IPC_REPLY: ebx=reply */
static int32_t sys_ipc_reply(const uint32_t *args) {
    struct ipc_message *user_reply = (struct ipc_message *)(uintptr_t)args[0];

    struct process *proc = process_current();
    if (!perm_check_name(proc, PERM_NODE_IPC_SEND)) {
        return -EPERM;
    }

    struct ipc_message *kreply = NULL;
    int                 ret    = ipc_msg_copy_in(&kreply, user_reply, true);
    if (ret < 0) {
        return ret;
    }

    ret = ipc_reply(kreply);
    ipc_msg_free(kreply);
    return ret;
}

/* SYS_IPC_REPLY_TO: ebx=sender_tid, ecx=reply */
static int32_t sys_ipc_reply_to(const uint32_t *args) {
    tid_t               sender_tid = (tid_t)args[0];
    struct ipc_message *user_reply = (struct ipc_message *)(uintptr_t)args[1];

    struct process *proc = process_current();
    if (!perm_check_name(proc, PERM_NODE_IPC_SEND)) {
        return -EPERM;
    }

    struct ipc_message *kreply = NULL;
    int                 ret    = ipc_msg_copy_in(&kreply, user_reply, true);
    if (ret < 0) {
        return ret;
    }

    ret = ipc_reply_to(sender_tid, kreply);
    ipc_msg_free(kreply);
    return ret;
}

/* SYS_NOTIFICATION_CREATE */
static int32_t sys_notification_create(const uint32_t *args) {
    (void)args;
    handle_t h = notification_create();
    if (h == HANDLE_INVALID) {
        return -ENOMEM;
    }
    return (int32_t)h;
}

/* SYS_NOTIFICATION_WAIT: ebx=handle */
static int32_t sys_notification_wait(const uint32_t *args) {
    handle_t h = (handle_t)args[0];
    return (int32_t)notification_wait(h);
}

/**
 * 注册 IPC 系统调用(新编号:100-119)
 */
void sys_ipc_init(void) {
    syscall_register(SYS_ENDPOINT_CREATE, sys_endpoint_create, 1, "endpoint_create");
    syscall_register(SYS_IPC_SEND, sys_ipc_send, 3, "ipc_send");
    syscall_register(SYS_IPC_SEND_ASYNC, sys_ipc_send_async, 2, "ipc_send_async");
    syscall_register(SYS_IPC_RECV, sys_ipc_recv, 3, "ipc_recv");
    syscall_register(SYS_IPC_CALL, sys_ipc_call, 4, "ipc_call");
    syscall_register(SYS_IPC_REPLY, sys_ipc_reply, 1, "ipc_reply");
    syscall_register(SYS_IPC_REPLY_TO, sys_ipc_reply_to, 2, "ipc_reply_to");
    /* 通知系统调用移至 800-819 范围 */
    syscall_register(SYS_NOTIFICATION_CREATE, sys_notification_create, 0, "notification_create");
    syscall_register(SYS_NOTIFICATION_WAIT, sys_notification_wait, 1, "notification_wait");
}
