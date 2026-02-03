/**
 * @file syscall.h
 * @brief 系统调用号和包装函数
 */

#ifndef _XNIX_SYSCALL_H
#define _XNIX_SYSCALL_H

#include <stddef.h>
#include <stdint.h>
#include <xnix/abi/capability.h>
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
static inline void sys_exit(int code) {
    syscall1(SYS_EXIT, (uint32_t)code);
    __builtin_unreachable();
}

static inline int sys_write(int fd, const void *buf, size_t len) {
    return syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)(uintptr_t)buf, (uint32_t)len);
}

static inline int sys_ioport_outb(uint32_t io_cap, uint16_t port, uint8_t val) {
    return syscall3(SYS_IOPORT_OUTB, io_cap, (uint32_t)port, (uint32_t)val);
}

static inline int sys_ioport_inb(uint32_t io_cap, uint16_t port) {
    return syscall2(SYS_IOPORT_INB, io_cap, (uint32_t)port);
}

static inline int sys_ioport_outw(uint32_t io_cap, uint16_t port, uint16_t val) {
    return syscall3(SYS_IOPORT_OUTW, io_cap, (uint32_t)port, (uint32_t)val);
}

static inline int sys_ioport_inw(uint32_t io_cap, uint16_t port) {
    return syscall2(SYS_IOPORT_INW, io_cap, (uint32_t)port);
}

static inline int sys_ioport_create_range(uint16_t start, uint16_t end, uint32_t rights) {
    return syscall3(SYS_IOPORT_CREATE_RANGE, (uint32_t)start, (uint32_t)end, rights);
}

static inline void sys_sleep(uint32_t ms) {
    syscall1(SYS_SLEEP, ms);
}

static inline int sys_module_count(void) {
    return syscall0(SYS_MODULE_COUNT);
}

static inline int sys_endpoint_create(void) {
    return syscall0(SYS_ENDPOINT_CREATE);
}

static inline int sys_notification_create(void) {
    return syscall0(SYS_NOTIFICATION_CREATE);
}

static inline uint32_t sys_notification_wait(uint32_t handle) {
    return (uint32_t)syscall1(SYS_NOTIFICATION_WAIT, handle);
}

/*
 * IPC 相关系统调用
 */
struct ipc_message;

static inline int sys_ipc_send(uint32_t ep, struct ipc_message *msg, uint32_t timeout_ms) {
    return syscall3(SYS_IPC_SEND, ep, (uint32_t)(uintptr_t)msg, timeout_ms);
}

static inline int sys_ipc_receive(uint32_t ep, struct ipc_message *msg, uint32_t timeout_ms) {
    return syscall3(SYS_IPC_RECV, ep, (uint32_t)(uintptr_t)msg, timeout_ms);
}

static inline int sys_ipc_call(uint32_t ep, struct ipc_message *req, struct ipc_message *reply,
                               uint32_t timeout_ms) {
    return syscall4(SYS_IPC_CALL, ep, (uint32_t)(uintptr_t)req, (uint32_t)(uintptr_t)reply,
                    timeout_ms);
}

static inline int sys_ipc_reply(struct ipc_message *reply) {
    return syscall1(SYS_IPC_REPLY, (uint32_t)(uintptr_t)reply);
}

static inline int sys_ipc_reply_to(uint32_t sender_tid, struct ipc_message *reply) {
    return syscall2(SYS_IPC_REPLY_TO, sender_tid, (uint32_t)(uintptr_t)reply);
}

/*
 * 进程管理
 *
 * spawn_cap 和 spawn_args 使用 ABI 定义的结构
 */
#define spawn_cap  abi_spawn_cap
#define spawn_args abi_spawn_args

/* 从 ABI 定义派生的权限宏 */
#define CAP_READ  ABI_CAP_READ
#define CAP_WRITE ABI_CAP_WRITE
#define CAP_GRANT ABI_CAP_GRANT

static inline int sys_spawn(struct spawn_args *args) {
    return syscall1(SYS_SPAWN, (uint32_t)(uintptr_t)args);
}

/* waitpid options */
#define WNOHANG 1

static inline int sys_waitpid(int pid, int *status, int options) {
    return syscall3(SYS_WAITPID, (uint32_t)pid, (uint32_t)(uintptr_t)status, (uint32_t)options);
}

static inline int sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

static inline int sys_getppid(void) {
    return syscall0(SYS_GETPPID);
}

static inline int sys_kill(int pid, int sig) {
    return syscall2(SYS_KILL, (uint32_t)pid, (uint32_t)sig);
}

/*
 * IRQ 绑定
 */
#define IRQ_READ_NONBLOCK 1

static inline int sys_irq_bind(uint8_t irq, uint32_t notif_handle, uint32_t bits) {
    return syscall3(SYS_IRQ_BIND, (uint32_t)irq, notif_handle, bits);
}

static inline int sys_irq_unbind(uint8_t irq) {
    return syscall1(SYS_IRQ_UNBIND, (uint32_t)irq);
}

static inline int sys_irq_read(uint8_t irq, void *buf, size_t size, uint32_t flags) {
    return syscall4(SYS_IRQ_READ, (uint32_t)irq, (uint32_t)(uintptr_t)buf, (uint32_t)size, flags);
}

/*
 * 内存管理
 */
static inline void *sys_sbrk(int32_t increment) {
    return (void *)syscall1(SYS_SBRK, (uint32_t)increment);
}

/*
 * Framebuffer
 */
struct abi_fb_info;

static inline int sys_fb_info(struct abi_fb_info *info) {
    return syscall1(SYS_FB_INFO, (uint32_t)(uintptr_t)info);
}

static inline void *sys_fb_map(void) {
    return (void *)syscall0(SYS_FB_MAP);
}

/*
 * Boot Module
 */
static inline void *sys_module_map(uint32_t index, uint32_t *size_out) {
    return (void *)syscall2(SYS_MODULE_MAP, index, (uint32_t)(uintptr_t)size_out);
}

/*
 * 进程列表
 */
#define PROC_NAME_MAX 16
#define PROCLIST_MAX  64

struct proc_info {
    int32_t  pid;
    int32_t  ppid;
    uint8_t  state; /* 0=RUNNING, 1=ZOMBIE */
    uint8_t  reserved[3];
    uint32_t thread_count;
    uint64_t cpu_ticks; /* 累计 CPU ticks */
    uint32_t heap_kb;   /* 堆内存(KB) */
    uint32_t stack_kb;  /* 栈内存(KB) */
    char     name[PROC_NAME_MAX];
};

struct sys_info {
    uint32_t cpu_count;   /* CPU 数量 */
    uint64_t total_ticks; /* 全局 tick 计数 */
    uint64_t idle_ticks;  /* idle tick 计数 */
};

struct proclist_args {
    struct proc_info *buf;
    uint32_t          buf_count;
    uint32_t          start_index;
    struct sys_info  *sys_info; /* 可为 NULL */
};

static inline int sys_proclist(struct proclist_args *args) {
    return syscall1(SYS_PROCLIST, (uint32_t)(uintptr_t)args);
}

int sys_exec(struct abi_exec_args *args);

#endif /* _XNIX_SYSCALL_H */
