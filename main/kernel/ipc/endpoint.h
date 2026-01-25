#ifndef KERNEL_IPC_ENDPOINT_H
#define KERNEL_IPC_ENDPOINT_H

#include <xnix/sync.h>
#include <xnix/types.h>

struct thread;

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
