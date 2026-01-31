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

/**
 * 直接使用 endpoint 指针进行 IPC call
 * 就是一个优化机制 前提是调用方要负责下面的事情.
 * 与普通 ipc_call(handle) 的区别:
 *   - ipc_call(handle):用户态调用,内核通过 handle 查能力表,验证权限后获取 ep
 *   - ipc_call_direct(ep):内核内部调用,跳过能力表查找,直接使用已验证的 ep
 *
 * 场景:
 *   内核子系统(如 VFS)在初始化时已通过 cap_lookup() 验证过 endpoint 的权限,
 *   后续操作不需要重复查表,直接用缓存的 ep 指针调用即可.
 *
 * 调用者责任:
 *   - 确保 ep 指针有效(已通过 cap_lookup 获取)
 *   - 确保 ep 的引用计数已增加(防止被释放)
 *
 * @param ep         已验证的 endpoint 指针
 * @param msg        发送的消息
 * @param reply_buf  接收回复的缓冲区
 * @param timeout_ms 超时时间(0 = 永久等待)
 * @return IPC_OK 成功,其他为错误码
 */
int ipc_call_direct(struct ipc_endpoint *ep, struct ipc_message *msg, struct ipc_message *reply_buf,
                    uint32_t timeout_ms);

#endif /* KERNEL_IPC_ENDPOINT_H */
