/**
 * @file endpoint.c
 * @brief IPC Endpoint 实现
 */

#include <kernel/capability/capability.h>
#include <kernel/ipc/endpoint.h>
#include <kernel/ipc/msg_pool.h>
#include <kernel/ipc/notification.h>
#include <kernel/sched/sched.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/process.h>
#include <xnix/string.h>
#include <xnix/sync.h>

#include "arch/cpu.h"

/*spin_unlock
 * Endpoint 对象管理
 * spin_unlock
 */

void endpoint_ref(void *ptr) {
    struct ipc_endpoint *ep = ptr;
    uint32_t             flags;

    if (!ep) {
        return;
    }

    flags = cpu_irq_save();
    ep->refcount++;
    cpu_irq_restore(flags);
}

void endpoint_unref(void *ptr) {
    struct ipc_endpoint *ep = ptr;
    uint32_t             flags;

    if (!ep) {
        return;
    }

    flags = cpu_irq_save();
    ep->refcount--;
    if (ep->refcount == 0) {
        cpu_irq_restore(flags);

        /* 释放资源前确保没有线程在等待(理论上 refcount=0 不应该有) */
        /* TODO: 如果有强行杀进程导致的情况,可能需要清理队列 */

        kfree(ep);
        return;
    }
    cpu_irq_restore(flags);
}

cap_handle_t endpoint_create(void) {
    struct ipc_endpoint *ep;
    cap_handle_t         handle;
    struct process      *current;

    current = process_current();
    if (!current) {
        return CAP_HANDLE_INVALID;
    }

    ep = kzalloc(sizeof(struct ipc_endpoint));
    if (!ep) {
        return CAP_HANDLE_INVALID;
    }

    spin_init(&ep->lock);
    ep->send_queue = NULL;
    ep->recv_queue = NULL;
    ep->refcount   = 0; /* cap_alloc 会增加引用计数 */
#if CFG_IPC_MSG_POOL
    ep->async_head = NULL;
    ep->async_tail = NULL;
    ep->async_len  = 0;
#else
    ep->async_head = 0;
    ep->async_tail = 0;
#endif

    /* 分配句柄: 默认给予读写和管理权限 */
    cap_rights_t rights = CAP_READ | CAP_WRITE | CAP_GRANT | CAP_MANAGE;
    handle              = cap_alloc(current, CAP_TYPE_ENDPOINT, ep, rights);

    if (handle == CAP_HANDLE_INVALID) {
        kfree(ep);
        return CAP_HANDLE_INVALID;
    }

    return handle;
}

/*spin_unlock
 * 消息传递辅助函数
 *spin_unlock */

/* 从 Sender 拷贝消息到 Receiver (包括寄存器和 Buffer) */
static void ipc_copy_msg(struct thread *src, struct thread *dst, struct ipc_message *src_msg,
                         struct ipc_message *dst_msg) {
    if (!src_msg || !dst_msg) {
        return;
    }

    /* 拷贝寄存器 */
    memcpy(&dst_msg->regs, &src_msg->regs, sizeof(struct ipc_msg_regs));

    /* 拷贝 Buffer */
    /* TODO: 校验 buffer 大小和有效性, 处理 page fault */
    if (src_msg->buffer.data && src_msg->buffer.size > 0 && dst_msg->buffer.data &&
        dst_msg->buffer.size >= src_msg->buffer.size) {
        memcpy(dst_msg->buffer.data, src_msg->buffer.data, src_msg->buffer.size);
        dst_msg->buffer.size = src_msg->buffer.size;
    } else {
        dst_msg->buffer.size = 0;
    }

    /* 拷贝能力句柄 */
    dst_msg->caps.count = 0;
}

/*spin_unlock
 * IPC 原语实现
 *spin_unlock */
static int ipc_send_internal(cap_handle_t ep_handle, struct ipc_message *msg,
                             struct ipc_message *reply_buf, uint32_t timeout_ms) {
    struct process      *proc = process_current();
    struct thread       *current;
    struct ipc_endpoint *ep;
    struct thread       *receiver;

    (void)timeout_ms; /* TODO: 支持超时 */

    if (!proc) {
        return IPC_ERR_INVALID;
    }

    /* 查找 Endpoint */
    ep = cap_lookup(proc, ep_handle, CAP_TYPE_ENDPOINT, CAP_WRITE);
    if (!ep) {
        return IPC_ERR_INVALID;
    }

    current = sched_current();

    spin_lock(&ep->lock);

    /* 检查是否有等待接收的线程 */
    if (ep->recv_queue) {
        receiver            = ep->recv_queue;
        ep->recv_queue      = receiver->wait_next; /* 移出队列 */
        receiver->wait_next = NULL;
        spin_unlock(&ep->lock);

        // 拷贝消息: 当前发送者 -> 接收者
        // 接收者正在 ipc_receive 中等待, 其 ipc_reply_msg 指向接收缓冲区
        // 注意: ipc_receive 会复用 ipc_reply_msg 字段来存储接收缓冲区指针
        ipc_copy_msg(current, receiver, msg, receiver->ipc_reply_msg);

        /* 记录发送者 TID,以便 Receiver 回复 */
        receiver->ipc_peer = current->tid;

        /* 唤醒接收者 */
        sched_wakeup_thread(receiver);

        /* 发送者进入等待回复状态 */
        /* 注: Rendezvous 模型 Send/Call 阻塞发送者,直到接收者回复*/
        current->ipc_req_msg   = msg;       /* 保存发送 buffer (保存起来便于内核随时访问) */
        current->ipc_reply_msg = reply_buf; /* 保存回复 buffer */

        /* 阻塞自己,等待 Reply */
        /* 注意: Receiver 处理完后会调用 ipc_reply(sender_tid) 来唤醒我们 */
        sched_block(current);

        return IPC_OK;
    }

    /* 没有接收者,加入发送队列 */
    current->wait_next = ep->send_queue;
    ep->send_queue     = current;
    spin_unlock(&ep->lock);

    /* 保存状态 */
    current->ipc_req_msg   = msg;
    current->ipc_reply_msg = reply_buf;

    /* 阻塞自己,等待接收者提取消息 */
    sched_block(current);

    // 在微内核同步 IPC 中,调用流程通常是 call/send → 阻塞等待 → receive → reply → 唤醒发送者
    /* 被唤醒了 */
    /* 情况 1: 接收者取走了消息 (Sender -> Receiver) */
    /* 此时依然需要等待 Reply*/
    /* 实际上,如果是这种情况, Receiver 取走消息时应该只是把我们从 send_queue 移走, */
    /* 但不应该唤醒我们(除非是爆炸了或超时). */
    /* 我们应该继续阻塞,直到 Reply 到达. */
    return IPC_OK;
}

int ipc_send(cap_handle_t ep_handle, struct ipc_message *msg, uint32_t timeout_ms) {
    /* Send 不关心 Reply 内容, 传入 NULL */
    return ipc_send_internal(ep_handle, msg, NULL, timeout_ms);
}

int ipc_call(cap_handle_t ep_handle, struct ipc_message *request, struct ipc_message *reply,
             uint32_t timeout_ms) {
    return ipc_send_internal(ep_handle, request, reply, timeout_ms);
}

int ipc_receive(cap_handle_t ep_handle, struct ipc_message *msg, uint32_t timeout_ms) {
    struct process      *proc = process_current();
    struct thread       *current;
    struct ipc_endpoint *ep;
    struct thread       *sender;

    (void)timeout_ms;

    if (!proc) {
        return IPC_ERR_INVALID;
    }

    ep = cap_lookup(proc, ep_handle, CAP_TYPE_ENDPOINT, CAP_READ);
    if (!ep) {
        return IPC_ERR_INVALID;
    }

    current = sched_current();

    spin_lock(&ep->lock);

    /* 优先检查异步消息队列 */
#if CFG_IPC_MSG_POOL
    if (ep->async_head) {
        struct ipc_kmsg *km = ep->async_head;
        ep->async_head      = km->next;
        if (!ep->async_head) {
            ep->async_tail = NULL;
        }
        if (ep->async_len) {
            ep->async_len--;
        }
        spin_unlock(&ep->lock);

        memcpy(&msg->regs, &km->regs, sizeof(struct ipc_msg_regs));
        ipc_kmsg_put(km);
        current->ipc_peer = TID_INVALID;
        return IPC_OK;
    }
#else
    if (ep->async_head != ep->async_tail) {
        /* 有缓存的异步消息,直接取出 */
        memcpy(&msg->regs, &ep->async_queue[ep->async_head].regs, sizeof(struct ipc_msg_regs));
        ep->async_head = (ep->async_head + 1) % IPC_ASYNC_QUEUE_SIZE;
        spin_unlock(&ep->lock);

        current->ipc_peer = TID_INVALID; /* 异步消息无需回复 */
        return IPC_OK;
    }
#endif

    /* 检查发送队列(同步发送者) */
    if (ep->send_queue) {
        sender            = ep->send_queue;
        ep->send_queue    = sender->wait_next;
        sender->wait_next = NULL;
        spin_unlock(&ep->lock);

        /* 拷贝消息: Sender -> Current(Receiver) */
        ipc_copy_msg(sender, current, sender->ipc_req_msg, msg);

        /* 记录发送者 */
        current->ipc_peer = sender->tid;

        /* 注意 不要唤醒 Sender!!!!!!!!!!!!!!!!!!!!!! Sender 继续阻塞等待 Reply */
        /* 只从 send_queue 移除了 Sender, 但它依然在 blocked_list 中 (wait_chan=Sender) */
        return IPC_OK;
    }

    /* 没有发送者,加入接收队列 */
    current->wait_next = ep->recv_queue;
    ep->recv_queue     = current;
    spin_unlock(&ep->lock);

    /* 保存接收 buffer 指针到 ipc_reply_msg (复用字段) */
    current->ipc_reply_msg = msg;

    /* 阻塞等待发送者 */
    sched_block(current);

    /* 被唤醒,说明收到消息了 */
    /* 此时 msg 已经被填充, ipc_peer 已经被设置 */

    return IPC_OK;
}

int ipc_send_async(cap_handle_t ep_handle, struct ipc_message *msg) {
    struct process      *proc = process_current();
    struct thread       *current;
    struct ipc_endpoint *ep;
    struct thread       *receiver;

    if (!proc) {
        return IPC_ERR_INVALID;
    }

    /* 查找 Endpoint */
    ep = cap_lookup(proc, ep_handle, CAP_TYPE_ENDPOINT, CAP_WRITE);
    if (!ep) {
        return IPC_ERR_INVALID;
    }

    current = sched_current();

    spin_lock(&ep->lock);

    /* 检查是否有等待接收的线程 */
    if (ep->recv_queue) {
        receiver            = ep->recv_queue;
        ep->recv_queue      = receiver->wait_next;
        receiver->wait_next = NULL;
        spin_unlock(&ep->lock);

        /* 拷贝消息: Current -> Receiver */
        ipc_copy_msg(current, receiver, msg, receiver->ipc_reply_msg);

        /* 异步发送不需要 Reply, 也不阻塞发送者 */
        receiver->ipc_peer = TID_INVALID; /* 标记为无 Reply */

        /* 唤醒接收者 */
        sched_wakeup_thread(receiver);

        return IPC_OK;
    }

    /* 没有接收者,尝试入队 */
#if CFG_IPC_MSG_POOL
    if (ep->async_len >= IPC_ASYNC_QUEUE_SIZE) {
        spin_unlock(&ep->lock);
        return IPC_ERR_TIMEOUT;
    }

    struct ipc_kmsg *km = ipc_kmsg_alloc();
    if (!km) {
        spin_unlock(&ep->lock);
        return IPC_ERR_NOMEM;
    }

    memcpy(&km->regs, &msg->regs, sizeof(struct ipc_msg_regs));
    km->next = NULL;

    if (ep->async_tail) {
        ep->async_tail->next = km;
    } else {
        ep->async_head = km;
    }
    ep->async_tail = km;
    ep->async_len++;

    spin_unlock(&ep->lock);
    return IPC_OK;
#else
    uint32_t next_tail = (ep->async_tail + 1) % IPC_ASYNC_QUEUE_SIZE;
    if (next_tail == ep->async_head) {
        /* 队列满 */
        spin_unlock(&ep->lock);
        return IPC_ERR_TIMEOUT;
    }

    /* 拷贝消息到队列 */
    memcpy(&ep->async_queue[ep->async_tail].regs, &msg->regs, sizeof(struct ipc_msg_regs));
    ep->async_tail = next_tail;

    spin_unlock(&ep->lock);
    return IPC_OK;
#endif
}

cap_handle_t ipc_wait_any(struct ipc_wait_set *set, uint32_t timeout_ms) {
    /* TODO: 实现NIO */
    (void)set;
    (void)timeout_ms;
    return CAP_HANDLE_INVALID;
}

int ipc_reply(struct ipc_message *reply) {
    struct thread *current = sched_current();
    struct thread *sender;
    tid_t          sender_tid;

    if (!current) {
        return IPC_ERR_INVALID;
    }

    sender_tid = current->ipc_peer;
    if (sender_tid == TID_INVALID) {
        return IPC_ERR_INVALID;
    }

    /* 查找等待 Reply 的发送者 */
    /* 发送者必须是阻塞状态 */
    sender = sched_lookup_blocked(sender_tid);
    if (!sender) {
        /* 发送者可能已经超时/被杀/不在阻塞状态 */
        return IPC_ERR_INVALID;
    }

    /* 拷贝 Reply: Current(Receiver) -> Sender */
    if (reply && sender->ipc_reply_msg) {
        ipc_copy_msg(current, sender, reply, sender->ipc_reply_msg);
    }

    /* 唤醒发送者 */
    sched_wakeup_thread(sender);

    return IPC_OK;
}

void ipc_init(void) {
    ipc_kmsg_pool_init();
    /* 注册 Endpoint 类型的能力操作 */
    cap_register_type(CAP_TYPE_ENDPOINT, endpoint_ref, endpoint_unref);
    /* 注册 Notification 类型的能力操作 */
    cap_register_type(CAP_TYPE_NOTIFICATION, notification_ref, notification_unref);
}
