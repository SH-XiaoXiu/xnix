#ifndef KERNEL_IPC_ENDPOINT_H
#define KERNEL_IPC_ENDPOINT_H

#include <xnix/ipc.h>
#include <xnix/sync.h>
#include <xnix/types.h>

struct thread;
struct ipc_kmsg;

/* 异步消息队列大小 (TODO: 换成动态实现) */
#define IPC_ASYNC_QUEUE_SIZE 64

/* 异步消息队列节点 */
struct ipc_async_msg {
    struct ipc_msg_regs regs; /* 只缓存寄存器部分 */
};

/**
 * IPC Endpoint 对象
 *
 * 包含发送和接收等待队列.
 * 锁保护队列的操作.
 */
struct ipc_endpoint {
    spinlock_t     lock;
    struct thread *send_queue; /* 等待发送的线程 */
    struct thread *recv_queue; /* 等待接收的线程 */
    uint32_t       refcount;

    /* 异步消息队列(环形缓冲区) */
#if CFG_IPC_MSG_POOL
    struct ipc_kmsg *async_head;
    struct ipc_kmsg *async_tail;
    uint32_t         async_len;
#else
    struct ipc_async_msg async_queue[IPC_ASYNC_QUEUE_SIZE];
    uint32_t             async_head; /* 读指针 */
    uint32_t             async_tail; /* 写指针 */
#endif
};

/**
 * 增加引用计数
 */
void endpoint_ref(void *ptr);

/**
 * 减少引用计数
 * 减到 0 时释放对象
 */
void endpoint_unref(void *ptr);

#endif /* KERNEL_IPC_ENDPOINT_H */
