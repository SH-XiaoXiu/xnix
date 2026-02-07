/**
 * @file endpoint.c
 * @brief IPC Endpoint 实现
 */

#include <arch/cpu.h>

#include <ipc/endpoint.h>
#include <ipc/notification.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/ipc.h>
#include <xnix/mm.h>
#include <xnix/perm.h>
#include <xnix/process.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/sync.h>
#include <xnix/thread.h>
#include <xnix/thread_def.h>

/*
 * 前向声明
 */
static void endpoint_poll_wakeup(struct ipc_endpoint *ep);

/*
 * Endpoint 对象管理
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
        kfree(ep);
        return;
    }
    cpu_irq_restore(flags);
}

handle_t endpoint_create(const char *name) {
    struct ipc_endpoint *ep;
    handle_t             handle;
    struct process      *current;

    current = process_current();
    if (!current) {
        return HANDLE_INVALID;
    }

    ep = kzalloc(sizeof(struct ipc_endpoint));
    if (!ep) {
        return HANDLE_INVALID;
    }

    spin_init(&ep->lock);
    ep->send_queue = NULL;
    ep->recv_queue = NULL;
    ep->poll_queue = NULL;
    ep->refcount   = 1; /* Handle 持有引用 */

    /* 分配句柄(传递名称用于权限注册) */
    handle = handle_alloc(current, HANDLE_ENDPOINT, ep, name);

    if (handle == HANDLE_INVALID) {
        kfree(ep);
        return HANDLE_INVALID;
    }

    pr_debug("[IPC] endpoint_create: name=%s handle=%d\n", name ? name : "null", handle);
    return handle;
}

/*
 * 消息传递辅助函数
 */

/**
 * 从 Sender 拷贝消息到 Receiver
 *
 * 注意:buffer.data 在此处已经是内核缓冲区(由 sys_ipc.c 的
 * copy_from_user/copy_to_user 处理),所以直接 memcpy 是安全的.
 */
static void ipc_copy_msg(struct thread *src, struct thread *dst, struct ipc_message *src_msg,
                         struct ipc_message *dst_msg) {
    if (!src_msg || !dst_msg) {
        return;
    }

    /* 拷贝寄存器 */
    memcpy(&dst_msg->regs, &src_msg->regs, sizeof(struct ipc_msg_regs));

    /* 拷贝 Buffer(内核缓冲区之间) */
    if (src_msg->buffer.data && src_msg->buffer.size > 0 && dst_msg->buffer.data &&
        dst_msg->buffer.size >= src_msg->buffer.size) {
        memcpy(dst_msg->buffer.data, src_msg->buffer.data, src_msg->buffer.size);
        dst_msg->buffer.size = src_msg->buffer.size;
    } else {
        dst_msg->buffer.size = 0;
    }

    /* 拷贝 Handle (handle transfer)*/
    dst_msg->handles.count = 0;
    if (src_msg->handles.count > 0 && src_msg->handles.count <= IPC_MSG_HANDLES_MAX) {
        struct process *src_proc = src->owner;
        struct process *dst_proc = dst->owner;

        if (src_proc && dst_proc) {
            if (!perm_check_name(src_proc, PERM_NODE_HANDLE_GRANT)) {
                return;
            }
            for (uint32_t i = 0; i < src_msg->handles.count; i++) {
                handle_t src_handle = src_msg->handles.handles[i];

                /* 传递 Handle 到接收者进程 */
                handle_t dst_handle =
                    handle_transfer(src_proc, src_handle, dst_proc, NULL, HANDLE_INVALID);

                if (dst_handle != HANDLE_INVALID) {
                    dst_msg->handles.handles[dst_msg->handles.count++] = dst_handle;
                }
            }
        }
    }
}

/*
 * IPC 原语实现
 */

/**
 * IPC 发送核心实现
 * 直接使用 endpoint 指针,所有 send/call 操作都通过此函数
 */
static int ipc_send_to_ep(struct ipc_endpoint *ep, struct ipc_message *msg,
                          struct ipc_message *reply_buf, uint32_t timeout_ms) {
    struct thread *current = sched_current();
    if (!current) {
        return -EINVAL;
    }

    spin_lock(&ep->lock);

    /* 检查是否有等待接收的线程 */
    if (ep->recv_queue) {
        struct thread *receiver = ep->recv_queue;
        ep->recv_queue          = receiver->wait_next;
        receiver->wait_next     = NULL;
        spin_unlock(&ep->lock);
        endpoint_unref(ep);

        /* 拷贝消息到接收者 */
        ipc_copy_msg(current, receiver, msg, receiver->ipc_reply_msg);
        receiver->ipc_peer                  = current->tid;
        receiver->ipc_reply_msg->sender_tid = current->tid; /* 填充 sender_tid */
        sched_wakeup_thread(receiver);

        pr_debug("[IPC] send -> recv: sender=%d receiver=%d\n", current->tid, receiver->tid);

        /* 保存发送和回复缓冲区 */
        current->ipc_req_msg   = msg;
        current->ipc_reply_msg = reply_buf;

        /* 阻塞等待回复 */
        if (!sched_block_timeout(current, timeout_ms)) {
            pr_debug("[IPC] send reply timeout: sender=%d\n", current->tid);
            return -ETIMEDOUT;
        }
        return 0;
    }

    /* 没有接收者,加入发送队列 */
    endpoint_ref(ep);
    current->wait_next = ep->send_queue;
    ep->send_queue     = current;
    endpoint_poll_wakeup(ep);
    spin_unlock(&ep->lock);

    pr_debug("[IPC] send enqueue: sender=%d ep=%p\n", current->tid, ep);

    /* 保存状态 */
    current->ipc_req_msg   = msg;
    current->ipc_reply_msg = reply_buf;

    /* 阻塞等待接收者 */
    if (!sched_block_timeout(current, timeout_ms)) {
        /* 超时,从发送队列中移除自己 */
        pr_debug("[IPC] send wait receiver timeout: sender=%d\n", current->tid);
        spin_lock(&ep->lock);
        struct thread **pp = &ep->send_queue;
        while (*pp) {
            if (*pp == current) {
                *pp = current->wait_next;
                break;
            }
            pp = &(*pp)->wait_next;
        }
        current->wait_next = NULL;
        spin_unlock(&ep->lock);
        endpoint_unref(ep);
        return -ETIMEDOUT;
    }

    return 0;
}

int ipc_send(handle_t ep_handle, struct ipc_message *msg, uint32_t timeout_ms) {
    struct process     *proc = process_current();
    struct handle_entry entry;
    if (!proc) {
        return -EINVAL;
    }

    if (handle_acquire(proc, ep_handle, HANDLE_ENDPOINT, &entry) < 0) {
        return -EINVAL;
    }

    struct ipc_endpoint *ep = entry.object;
    if (entry.perm_send == PERM_ID_INVALID || !perm_check(proc, entry.perm_send)) {
        handle_object_put(entry.type, entry.object);
        return -EINVAL;
    }

    int ret = ipc_send_to_ep(ep, msg, NULL, timeout_ms);
    handle_object_put(entry.type, entry.object);
    return ret;
}

int ipc_call(handle_t ep_handle, struct ipc_message *request, struct ipc_message *reply,
             uint32_t timeout_ms) {
    struct process     *proc = process_current();
    struct handle_entry entry;
    if (!proc) {
        return -EINVAL;
    }

    if (handle_acquire(proc, ep_handle, HANDLE_ENDPOINT, &entry) < 0) {
        return -EINVAL;
    }

    struct ipc_endpoint *ep = entry.object;
    if (entry.perm_send == PERM_ID_INVALID || !perm_check(proc, entry.perm_send)) {
        handle_object_put(entry.type, entry.object);
        return -EINVAL;
    }

    int ret = ipc_send_to_ep(ep, request, reply, timeout_ms);
    handle_object_put(entry.type, entry.object);
    return ret;
}

int ipc_call_direct(struct ipc_endpoint *ep, struct ipc_message *msg, struct ipc_message *reply_buf,
                    uint32_t timeout_ms) {
    if (!ep) {
        return -EINVAL;
    }
    return ipc_send_to_ep(ep, msg, reply_buf, timeout_ms);
}

int ipc_receive(handle_t ep_handle, struct ipc_message *msg, uint32_t timeout_ms) {
    struct process     *proc = process_current();
    struct thread      *current;
    struct thread      *sender;
    struct handle_entry entry;

    if (!proc) {
        return -EINVAL;
    }

    if (handle_acquire(proc, ep_handle, HANDLE_ENDPOINT, &entry) < 0) {
        return -EINVAL;
    }

    struct ipc_endpoint *ep = entry.object;
    if (entry.perm_recv == PERM_ID_INVALID || !perm_check(proc, entry.perm_recv)) {
        handle_object_put(entry.type, entry.object);
        return -EINVAL;
    }

    current = sched_current();

    spin_lock(&ep->lock);

    /* 检查发送队列(同步发送者) */
    if (ep->send_queue) {
        sender            = ep->send_queue;
        ep->send_queue    = sender->wait_next;
        sender->wait_next = NULL;
        spin_unlock(&ep->lock);
        endpoint_unref(ep);

        /* 拷贝消息: Sender -> Current(Receiver) */
        ipc_copy_msg(sender, current, sender->ipc_req_msg, msg);

        /* 记录发送者 */
        current->ipc_peer = sender->tid;
        msg->sender_tid   = sender->tid; /* 填充 sender_tid 用于延迟回复 */

        pr_debug("[IPC] recv <- send: receiver=%d sender=%d\n", current->tid, sender->tid);

        /* 注意 不要唤醒 Sender!!!!!!!!!!!!!!!!!!!!!! Sender 继续阻塞等待 Reply */
        /* 只从 send_queue 移除了 Sender, 但它依然在 blocked_list 中 (wait_chan=Sender) */
        handle_object_put(entry.type, entry.object);
        return 0;
    }

    /* 没有发送者,加入接收队列 */
    endpoint_ref(ep);
    current->wait_next = ep->recv_queue;
    ep->recv_queue     = current;
    spin_unlock(&ep->lock);

    pr_debug("[IPC] recv enqueue: receiver=%d ep=%p\n", current->tid, ep);

    /* 保存接收 buffer 指针到 ipc_reply_msg(复用字段) */
    current->ipc_reply_msg = msg;

    /* 阻塞等待发送者 */
    if (!sched_block_timeout(current, timeout_ms)) {
        /* 超时,从接收队列中移除自己 */
        pr_debug("[IPC] recv timeout: receiver=%d\n", current->tid);
        spin_lock(&ep->lock);
        struct thread **pp = &ep->recv_queue;
        while (*pp) {
            if (*pp == current) {
                *pp = current->wait_next;
                break;
            }
            pp = &(*pp)->wait_next;
        }
        current->wait_next = NULL;
        spin_unlock(&ep->lock);
        endpoint_unref(ep);
        handle_object_put(entry.type, entry.object);
        return -ETIMEDOUT;
    }

    /* 被唤醒,说明收到消息了 */
    handle_object_put(entry.type, entry.object);
    return 0;
}

/**
 * 检查 endpoint 是否有待接收消息(不加锁版本,调用者持锁)
 */
static bool endpoint_has_message_locked(struct ipc_endpoint *ep) {
    return ep->send_queue != NULL;
}

/**
 * 检查 notification 是否有待接收信号(不加锁版本,调用者持锁)
 */
static bool notification_has_signal_locked(struct ipc_notification *notif) {
    return notif->pending_bits != 0;
}

/**
 * 唤醒 endpoint poll_queue 中的所有等待者
 * 调用者必须持有 ep->lock
 */
static void endpoint_poll_wakeup(struct ipc_endpoint *ep) {
    struct poll_entry *pe = ep->poll_queue;
    while (pe) {
        pe->triggered = true;
        sched_wakeup_thread(pe->waiter);
        pe = pe->next;
    }
}

/**
 * 从 endpoint poll_queue 中移除指定 poll_entry
 * 调用者必须持有 ep->lock
 */
static void endpoint_poll_remove(struct ipc_endpoint *ep, struct poll_entry *entry) {
    struct poll_entry **pp = &ep->poll_queue;
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->next;
            return;
        }
        pp = &(*pp)->next;
    }
}

/**
 * 从 notification poll_queue 中移除指定 poll_entry
 * 调用者必须持有 notif->lock
 */
static void notification_poll_remove(struct ipc_notification *notif, struct poll_entry *entry) {
    struct poll_entry **pp = &notif->poll_queue;
    while (*pp) {
        if (*pp == entry) {
            *pp = entry->next;
            return;
        }
        pp = &(*pp)->next;
    }
}

/**
 * 等待多个 endpoint/notification 中的任意一个就绪
 *
 * 事件驱动实现:
 * 1. 首先检查是否有已就绪的对象
 * 2. 为每个对象创建 poll_entry 加入其 poll_queue
 * 3. 阻塞等待
 * 4. 被唤醒后找出就绪的对象
 * 5. 清理所有 poll_entry
 */
handle_t ipc_wait_any(struct ipc_wait_set *set, uint32_t timeout_ms) {
    struct process *proc    = process_current();
    struct thread  *current = sched_current();
    handle_t        result  = HANDLE_INVALID;

    if (!set || set->count == 0 || set->count > IPC_WAIT_MAX || !proc || !current) {
        return HANDLE_INVALID;
    }

    /* 在栈上分配 poll entries */
    struct poll_entry entries[IPC_WAIT_MAX];
    void             *objects[IPC_WAIT_MAX]; /* 对象指针 */
    handle_type_t     types[IPC_WAIT_MAX];   /* 对象类型 */
    uint32_t          valid_count = 0;

    /* 第一遍:查找所有对象,检查是否有已就绪的 */
    for (uint32_t i = 0; i < set->count; i++) {
        handle_t handle = set->handles[i];

        struct handle_entry entry;
        if (handle_acquire(proc, handle, HANDLE_NONE, &entry) < 0) {
            continue;
        }

        if (entry.type == HANDLE_ENDPOINT) {
            /* 检查 recv 权限 */
            if (entry.perm_recv == PERM_ID_INVALID || !perm_check(proc, entry.perm_recv)) {
                handle_object_put(entry.type, entry.object);
                continue;
            }

            struct ipc_endpoint *ep = entry.object;
            spin_lock(&ep->lock);
            if (endpoint_has_message_locked(ep)) {
                spin_unlock(&ep->lock);
                pr_debug("[IPC] wait_any ready: tid=%d handle=%d\n", current->tid, handle);
                handle_object_put(entry.type, entry.object);
                for (uint32_t j = 0; j < valid_count; j++) {
                    handle_object_put(types[j], objects[j]);
                }
                return handle;
            }
            spin_unlock(&ep->lock);

            objects[valid_count]           = ep;
            types[valid_count]             = HANDLE_ENDPOINT;
            entries[valid_count].handle    = handle;
            entries[valid_count].waiter    = current;
            entries[valid_count].triggered = false;
            entries[valid_count].next      = NULL;
            valid_count++;
        } else if (entry.type == HANDLE_NOTIFICATION) {
            struct ipc_notification *notif = entry.object;
            spin_lock(&notif->lock);
            if (notification_has_signal_locked(notif)) {
                spin_unlock(&notif->lock);
                pr_debug("[IPC] wait_any ready: tid=%d handle=%d\n", current->tid, handle);
                handle_object_put(entry.type, entry.object);
                for (uint32_t j = 0; j < valid_count; j++) {
                    handle_object_put(types[j], objects[j]);
                }
                return handle;
            }
            spin_unlock(&notif->lock);

            objects[valid_count]           = notif;
            types[valid_count]             = HANDLE_NOTIFICATION;
            entries[valid_count].handle    = handle;
            entries[valid_count].waiter    = current;
            entries[valid_count].triggered = false;
            entries[valid_count].next      = NULL;
            valid_count++;
        } else {
            handle_object_put(entry.type, entry.object);
        }
    }

    if (valid_count == 0) {
        return HANDLE_INVALID;
    }

    /* 第二遍:将 poll_entry 加入各对象的 poll_queue */
    for (uint32_t i = 0; i < valid_count; i++) {
        if (types[i] == HANDLE_ENDPOINT) {
            struct ipc_endpoint *ep = objects[i];
            spin_lock(&ep->lock);
            entries[i].next = ep->poll_queue;
            ep->poll_queue  = &entries[i];
            spin_unlock(&ep->lock);
        } else {
            struct ipc_notification *notif = objects[i];
            spin_lock(&notif->lock);
            entries[i].next   = notif->poll_queue;
            notif->poll_queue = &entries[i];
            spin_unlock(&notif->lock);
        }
    }

    /* 阻塞等待 */
    pr_debug("[IPC] wait_any block: tid=%d count=%d\n", current->tid, set->count);
    if (!sched_block_timeout(current, timeout_ms)) {
        result = HANDLE_INVALID; /* 超时 */
    } else {
        /* 找出被触发的 entry */
        for (uint32_t i = 0; i < valid_count; i++) {
            if (entries[i].triggered) {
                result = entries[i].handle;
                pr_debug("[IPC] wait_any wakeup: tid=%d handle=%d\n", current->tid, result);
                break;
            }
        }
    }

    /* 清理:从所有对象的 poll_queue 中移除 entries */
    for (uint32_t i = 0; i < valid_count; i++) {
        if (types[i] == HANDLE_ENDPOINT) {
            struct ipc_endpoint *ep = objects[i];
            spin_lock(&ep->lock);
            endpoint_poll_remove(ep, &entries[i]);
            spin_unlock(&ep->lock);
        } else {
            struct ipc_notification *notif = objects[i];
            spin_lock(&notif->lock);
            notification_poll_remove(notif, &entries[i]);
            spin_unlock(&notif->lock);
        }
    }

    for (uint32_t i = 0; i < valid_count; i++) {
        handle_object_put(types[i], objects[i]);
    }

    return result;
}

int ipc_reply(struct ipc_message *reply) {
    struct thread *current = sched_current();
    struct thread *sender;
    tid_t          sender_tid;

    if (!current) {
        return -EINVAL;
    }

    sender_tid = current->ipc_peer;
    if (sender_tid == TID_INVALID) {
        return -EINVAL;
    }

    /* 查找等待 Reply 的发送者 */
    /* 发送者必须是阻塞状态 */
    sender = sched_lookup_blocked(sender_tid);
    if (!sender) {
        /* 发送者可能已经超时/被杀/不在阻塞状态 */
        return -EINVAL;
    }

    /* 拷贝 Reply: Current(Receiver) -> Sender */
    if (reply && sender->ipc_reply_msg) {
        ipc_copy_msg(current, sender, reply, sender->ipc_reply_msg);
    }

    /* 唤醒发送者 */
    sched_wakeup_thread(sender);

    pr_debug("[IPC] reply: sender=%d receiver=%d\n", current->tid, sender->tid);

    return 0;
}

/**
 * 延迟回复: 回复指定的发送者
 * 用于服务器需要延迟回复客户端的场景
 *
 * @param sender_tid 发送者的 TID (从 msg->sender_tid 获取)
 * @param reply      回复消息
 * @return 0 成功, 其他值表示错误
 */
int ipc_reply_to(tid_t sender_tid, struct ipc_message *reply) {
    struct thread *current = sched_current();
    struct thread *sender;

    if (!current || sender_tid == TID_INVALID) {
        return -EINVAL;
    }

    /* 查找等待 Reply 的发送者 */
    sender = sched_lookup_blocked(sender_tid);
    if (!sender) {
        /* 发送者可能已经超时/被杀/不在阻塞状态 */
        return -EINVAL;
    }

    /* 拷贝 Reply: Current -> Sender */
    if (reply && sender->ipc_reply_msg) {
        ipc_copy_msg(current, sender, reply, sender->ipc_reply_msg);
    }

    /* 唤醒发送者 */
    sched_wakeup_thread(sender);

    pr_debug("[IPC] reply_to: sender=%d receiver=%d\n", current->tid, sender->tid);

    return 0;
}

void ipc_init(void) {
    /* Handle 系统负责资源释放,不需要注册类型回调 */
}
