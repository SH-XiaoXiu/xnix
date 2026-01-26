/**
 * @file syscall.h
 * @brief 系统调用号和包装函数
 */

#ifndef _XNIX_SYSCALL_H
#define _XNIX_SYSCALL_H

#include <stdint.h>

/* 系统调用号 */
#define SYS_PUTC            1
#define SYS_EXIT            2
#define SYS_ENDPOINT_CREATE 3
#define SYS_IPC_SEND        4
#define SYS_IPC_RECV        5
#define SYS_IPC_CALL        6
#define SYS_IPC_REPLY       7
#define SYS_IOPORT_OUTB     8
#define SYS_IOPORT_INB      9
#define SYS_SLEEP           10
#define SYS_SPAWN           11
#define SYS_MODULE_COUNT    12

/* 系统调用内联包装 */
static inline int syscall0(int num) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num));
    return ret;
}

static inline int syscall1(int num, uint32_t arg1) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1));
    return ret;
}

static inline int syscall2(int num, uint32_t arg1, uint32_t arg2) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2));
    return ret;
}

static inline int syscall3(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3));
    return ret;
}

/* 高层系统调用包装 */
static inline void sys_exit(int code) {
    syscall1(SYS_EXIT, (uint32_t)code);
    __builtin_unreachable();
}

static inline void sys_putc(char c) {
    syscall1(SYS_PUTC, (uint32_t)(uint8_t)c);
}

static inline int sys_ioport_outb(uint32_t io_cap, uint16_t port, uint8_t val) {
    return syscall3(SYS_IOPORT_OUTB, io_cap, (uint32_t)port, (uint32_t)val);
}

static inline int sys_ioport_inb(uint32_t io_cap, uint16_t port) {
    return syscall2(SYS_IOPORT_INB, io_cap, (uint32_t)port);
}

/* IPC 相关系统调用 - 需要 ipc.h 中的结构体 */
struct ipc_message;

static inline int sys_ipc_send(uint32_t ep, struct ipc_message *msg, uint32_t timeout_ms) {
    return syscall3(SYS_IPC_SEND, ep, (uint32_t)(uintptr_t)msg, timeout_ms);
}

static inline int sys_ipc_receive(uint32_t ep, struct ipc_message *msg, uint32_t timeout_ms) {
    return syscall3(SYS_IPC_RECV, ep, (uint32_t)(uintptr_t)msg, timeout_ms);
}

static inline void sys_sleep(uint32_t ms) {
    syscall1(SYS_SLEEP, ms);
}

/* capability 权限定义 */
#define CAP_READ   (1 << 0)
#define CAP_WRITE  (1 << 1)
#define CAP_GRANT  (1 << 2)

/* spawn 相关结构 */
struct spawn_cap {
    uint32_t src;       /* 源 capability handle */
    uint32_t rights;    /* 授予的权限 */
    uint32_t dst_hint;  /* 期望的目标 handle（-1 表示任意） */
};

struct spawn_args {
    uint32_t         module_index;  /* 启动模块索引 */
    uint32_t         cap_count;     /* 传递的 capability 数量 */
    struct spawn_cap caps[8];       /* 最多传递 8 个 capability */
};

static inline int sys_spawn(struct spawn_args *args) {
    return syscall1(SYS_SPAWN, (uint32_t)(uintptr_t)args);
}

static inline int sys_module_count(void) {
    return syscall0(SYS_MODULE_COUNT);
}

#endif /* _XNIX_SYSCALL_H */
