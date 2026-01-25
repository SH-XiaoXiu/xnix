#ifndef KERNEL_IPC_MSG_POOL_H
#define KERNEL_IPC_MSG_POOL_H

#include <arch/atomic.h>

#include <xnix/config.h>
#include <xnix/ipc.h>
#include <xnix/types.h>

/**
 * @file msg_pool.h
 * @brief IPC 异步消息节点池(可裁切)
 *
 * 设计目标:
 * - 替换 endpoint 内部的静态异步环形缓冲区,提供更健壮的"全局消息池 + 引用计数"方案.
 * - 当 CFG_IPC_MSG_POOL=1:启用全局池,异步发送路径从池中申请节点并入队,接收侧出队后归还.
 * - 当 CFG_IPC_MSG_POOL=0:完全裁切,此文件只保留空实现占位,IPC 退化回旧的静态方案.
 *
 * 约束与语义:
 * - ipc_kmsg 只缓存 regs(短消息),不缓存 buffer/caps(目前异步队列仅承载 regs).
 * - refcount 用于安全复用节点:获取=+1,释放=-1,降到 0 才回收到池中.
 * - 线程安全:池的 free-list 由自旋锁保护;refcount 用原子操作;调用方无需额外加锁.
 */

/**
 * @brief 内核态异步消息节点
 *
 * 用途:
 * - 作为 endpoint 异步队列的链表节点.
 * - 节点内仅保存 regs,避免复制大 buffer 造成的内存压力与复杂的生命周期管理.
 */
struct ipc_kmsg {
    atomic_t            refcount;
    struct ipc_msg_regs regs;
    struct ipc_kmsg    *next;
};

#if CFG_IPC_MSG_POOL
/**
 * @brief 初始化全局消息池
 *
 * 在 IPC 子系统初始化时调用一次即可.
 * 初始化失败会降级为"池为空",后续 ipc_kmsg_alloc() 返回 NULL(由上层选择退化行为).
 */
void ipc_kmsg_pool_init(void);

/**
 * @brief 从全局池申请一个消息节点
 * @return 成功返回节点指针(refcount=1),失败返回 NULL
 *
 * 说明:
 * - 实现允许按需扩容(以固定 chunk 增长),但不会在调用方持有 endpoint 锁时做复杂操作.
 * - 返回的节点内容仅保证 next 被清空;regs 由调用方自行写入.
 */
struct ipc_kmsg *ipc_kmsg_alloc(void);

/**
 * @brief 增加节点引用计数
 *
 * 用于同一节点被多个队列/路径共享的场景(当前异步队列为单持有者,也可以不调用).
 */
void ipc_kmsg_get(struct ipc_kmsg *m);

/**
 * @brief 减少节点引用计数并在为 0 时回收
 *
 * 注意:
 * - 只要调用方持有的最后一个引用释放,节点会回到全局 free-list,可被再次分配.
 * - 不允许对未通过 ipc_kmsg_alloc() 获取的内存调用此函数.
 */
void ipc_kmsg_put(struct ipc_kmsg *m);
#else
static inline void ipc_kmsg_pool_init(void) {
}
#endif

#endif
