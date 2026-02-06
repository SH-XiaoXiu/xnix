/**
 * @file syscall.h
 * @brief 系统调用号和包装函数
 */

#ifndef _XNIX_SYSCALL_H
#define _XNIX_SYSCALL_H

#include <stddef.h>
#include <stdint.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/irq.h>
#include <xnix/abi/process.h>
#include <xnix/abi/syscall.h>

/*
 * 系统调用内联包装(x86 调用约定)
 */
static inline int syscall0(int num) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num) : "cc", "memory");
    return ret;
}

static inline int syscall1(int num, uint32_t arg1) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "b"(arg1) : "cc", "memory");
    return ret;
}

static inline int syscall2(int num, uint32_t arg1, uint32_t arg2) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "b"(arg1), "c"(arg2) : "cc", "memory");
    return ret;
}

static inline int syscall3(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "0"(num), "b"(arg1), "c"(arg2), "d"(arg3)
                     : "cc", "memory");
    return ret;
}

static inline int syscall4(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "0"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4)
                     : "cc", "memory");
    return ret;
}

static inline int syscall5(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4,
                           uint32_t arg5) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "0"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4), "D"(arg5)
                     : "cc", "memory");
    return ret;
}

/*
 * 高层系统调用包装
 */

/**
 * 退出当前进程
 *
 * @param code 退出码
 */
static inline void sys_exit(int code) {
    syscall1(SYS_EXIT, (uint32_t)code);
    __builtin_unreachable();
}

/**
 * 关闭句柄
 *
 * @param handle 要关闭的 handle
 * @return 0 成功,负数失败
 */
static inline int sys_handle_close(uint32_t handle) {
    return syscall1(SYS_HANDLE_CLOSE, handle);
}

/**
 * 复制句柄
 *
 * @param src 源 handle
 * @param dst_hint 目标 slot hint
 * @param name 新 handle 名称
 * @return 新 handle 值,负数失败
 */
static inline int sys_handle_duplicate(uint32_t src, uint32_t dst_hint, const char *name) {
    return syscall3(SYS_HANDLE_DUPLICATE, src, dst_hint, (uint32_t)(uintptr_t)name);
}

/**
 * 检查权限
 *
 * @param perm_id 权限 ID
 * @return 0 有权限,负数无权限
 */
static inline int sys_perm_check(uint32_t perm_id) {
    return syscall1(SYS_PERM_CHECK, perm_id);
}

/**
 * 按名称查找 handle
 *
 * @param name handle 名称
 * @return handle 值,负数失败
 */
static inline handle_t sys_handle_find(const char *name) {
    return (handle_t)syscall1(SYS_HANDLE_FIND, (uint32_t)(uintptr_t)name);
}

/**
 * 写 I/O 端口(8 位)
 */
static inline int sys_ioport_outb(uint16_t port, uint8_t val) {
    return syscall2(SYS_IOPORT_OUTB, (uint32_t)port, (uint32_t)val);
}

/**
 * 读 I/O 端口(8 位)
 */
static inline int sys_ioport_inb(uint16_t port) {
    return syscall1(SYS_IOPORT_INB, (uint32_t)port);
}

/**
 * 写 I/O 端口(16 位)
 */
static inline int sys_ioport_outw(uint16_t port, uint16_t val) {
    return syscall2(SYS_IOPORT_OUTW, (uint32_t)port, (uint32_t)val);
}

/**
 * 读 I/O 端口(16 位)
 */
static inline uint16_t sys_ioport_inw(uint16_t port) {
    return syscall1(SYS_IOPORT_INW, (uint32_t)port);
}

/**
 * 睡眠
 *
 * @param ms 毫秒数
 */
static inline void sys_sleep(uint32_t ms) {
    syscall1(SYS_SLEEP, ms);
}

/**
 * 创建 Endpoint
 *
 * @return Handle 值
 */
static inline int sys_endpoint_create(const char *name) {
    return syscall1(SYS_ENDPOINT_CREATE, (uint32_t)name);
}

/**
 * 创建 Notification
 *
 * @return Handle 值
 */
static inline int sys_notification_create(void) {
    return syscall0(SYS_NOTIFICATION_CREATE);
}

/**
 * 等待 Notification
 *
 * @param handle Notification handle
 * @return 信号位掩码
 */
static inline uint32_t sys_notification_wait(uint32_t handle) {
    return (uint32_t)syscall1(SYS_NOTIFICATION_WAIT, handle);
}

/*
 * IPC 相关系统调用
 */
struct ipc_message;

/**
 * 发送 IPC 消息
 */
static inline int sys_ipc_send(uint32_t ep, struct ipc_message *msg, uint32_t timeout_ms) {
    return syscall3(SYS_IPC_SEND, ep, (uint32_t)(uintptr_t)msg, timeout_ms);
}

static inline int sys_ipc_send_async(uint32_t ep, struct ipc_message *msg) {
    return syscall2(SYS_IPC_SEND_ASYNC, ep, (uint32_t)(uintptr_t)msg);
}

/**
 * 接收 IPC 消息
 */
static inline int sys_ipc_receive(uint32_t ep, struct ipc_message *msg, uint32_t timeout_ms) {
    return syscall3(SYS_IPC_RECV, ep, (uint32_t)(uintptr_t)msg, timeout_ms);
}

/**
 * 发起 IPC 调用(RPC)
 */
static inline int sys_ipc_call(uint32_t ep, struct ipc_message *req, struct ipc_message *reply,
                               uint32_t timeout_ms) {
    return syscall4(SYS_IPC_CALL, ep, (uint32_t)(uintptr_t)req, (uint32_t)(uintptr_t)reply,
                    timeout_ms);
}

/**
 * 回复 IPC 调用
 */
static inline int sys_ipc_reply(struct ipc_message *reply) {
    return syscall1(SYS_IPC_REPLY, (uint32_t)(uintptr_t)reply);
}

/**
 * 延迟回复 IPC 调用
 */
static inline int sys_ipc_reply_to(uint32_t sender_tid, struct ipc_message *reply) {
    return syscall2(SYS_IPC_REPLY_TO, sender_tid, (uint32_t)(uintptr_t)reply);
}

/*
 * 进程管理
 *
 * spawn_args 使用 ABI 定义的结构
 */
#define spawn_args abi_spawn_args

/**
 * 创建新进程(spawn)
 */
static inline int sys_spawn(struct spawn_args *args) {
    return syscall1(SYS_SPAWN, (uint32_t)(uintptr_t)args);
}

/**
 * 等待子进程退出
 */
static inline int sys_waitpid(int pid, int *status, int options) {
    return syscall3(SYS_WAITPID, (uint32_t)pid, (uint32_t)(uintptr_t)status, (uint32_t)options);
}

/**
 * 获取当前进程 PID
 */
static inline int sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

/**
 * 获取父进程 PID
 */
static inline int sys_getppid(void) {
    return syscall0(SYS_GETPPID);
}

/**
 * 发送信号
 */
static inline int sys_kill(int pid, int sig) {
    return syscall2(SYS_KILL, (uint32_t)pid, (uint32_t)sig);
}

/*
 * IRQ 绑定
 */

/**
 * @brief 绑定 IRQ 到 Notification
 */
static inline int sys_irq_bind(uint8_t irq, uint32_t notif_handle, uint32_t bits) {
    return syscall3(SYS_IRQ_BIND, (uint32_t)irq, notif_handle, bits);
}

/**
 * @brief 解除 IRQ 绑定
 */
static inline int sys_irq_unbind(uint8_t irq) {
    return syscall1(SYS_IRQ_UNBIND, (uint32_t)irq);
}

/**
 * @brief 读取 IRQ 数据
 */
static inline int sys_irq_read(uint8_t irq, void *buf, size_t size, uint32_t flags) {
    return syscall4(SYS_IRQ_READ, (uint32_t)irq, (uint32_t)(uintptr_t)buf, (uint32_t)size, flags);
}

/*
 * 内存管理
 */
/**
 * @brief 调整堆大小
 */
static inline void *sys_sbrk(int32_t increment) {
    return (void *)syscall1(SYS_SBRK, (uint32_t)increment);
}

/**
 * @brief 映射物理内存到用户空间
 *
 * @param handle   HANDLE_PHYSMEM 类型的 handle
 * @param offset   资源内偏移
 * @param size     映射大小 (0 = 整个区域)
 * @param prot     保护标志
 * @param out_size 可选输出参数: 实际映射的大小
 * @return 用户空间虚拟地址,失败返回 (void*)-1
 */
static inline void *sys_mmap_phys(handle_t handle, uint32_t offset, uint32_t size, uint32_t prot,
                                  uint32_t *out_size) {
    return (void *)syscall5(SYS_MMAP_PHYS, handle, offset, size, prot,
                            (uint32_t)(uintptr_t)out_size);
}

/**
 * Physmem 信息结构(用于 sys_physmem_info)
 */
struct physmem_info {
    uint32_t size;       /* 区域大小 */
    uint32_t type;       /* 0=generic, 1=fb */
    uint32_t width;      /* FB 宽度(仅 type=1) */
    uint32_t height;     /* FB 高度(仅 type=1) */
    uint32_t pitch;      /* FB pitch(仅 type=1) */
    uint8_t  bpp;        /* FB bpp(仅 type=1) */
    uint8_t  red_pos;    /* (仅 type=1) */
    uint8_t  red_size;   /* (仅 type=1) */
    uint8_t  green_pos;  /* (仅 type=1) */
    uint8_t  green_size; /* (仅 type=1) */
    uint8_t  blue_pos;   /* (仅 type=1) */
    uint8_t  blue_size;  /* (仅 type=1) */
    uint8_t  _reserved[5];
};

/**
 * @brief 查询物理内存区域信息
 *
 * @param handle HANDLE_PHYSMEM 类型的 handle
 * @param info   输出信息结构
 * @return 0 成功, 负数失败
 */
static inline int sys_physmem_info(handle_t handle, struct physmem_info *info) {
    return syscall2(SYS_PHYSMEM_INFO, handle, (uint32_t)(uintptr_t)info);
}

/*
 * 进程列表
 */
#define PROC_NAME_MAX 16
#define PROCLIST_MAX  64

/**
 * @brief 进程信息结构
 */
struct proc_info {
    int32_t  pid;
    int32_t  ppid;
    uint8_t  state; /**< 0=RUNNING, 1=ZOMBIE */
    uint8_t  reserved[3];
    uint32_t thread_count;
    uint64_t cpu_ticks; /**< 累计 CPU ticks */
    uint32_t heap_kb;   /**< 堆内存(KB) */
    uint32_t stack_kb;  /**< 栈内存(KB) */
    char     name[PROC_NAME_MAX];
};

/**
 * @brief 系统信息结构
 */
struct sys_info {
    uint32_t cpu_count;   /**< CPU 数量 */
    uint64_t total_ticks; /**< 全局 tick 计数 */
    uint64_t idle_ticks;  /**< idle tick 计数 */
};

/**
 * @brief proclist 系统调用参数
 */
struct proclist_args {
    struct proc_info *buf;
    uint32_t          buf_count;
    uint32_t          start_index;
    struct sys_info  *sys_info; /**< 可为 NULL */
};

/**
 * @brief 获取进程列表
 */
static inline int sys_proclist(struct proclist_args *args) {
    return syscall1(SYS_PROCLIST, (uint32_t)(uintptr_t)args);
}

int sys_exec(struct abi_exec_args *args);

/**
 * 读取内核日志条目
 *
 * @param seq  输入/输出:当前序列号
 * @param buf  输出缓冲区
 * @param size 缓冲区大小
 * @return 读取的字节数,负数为错误
 */
static inline int sys_kmsg_read(uint32_t *seq, char *buf, uint32_t size) {
    return syscall3(SYS_KMSG_READ, (uint32_t)(uintptr_t)seq, (uint32_t)(uintptr_t)buf, size);
}

#endif /* _XNIX_SYSCALL_H */
