/**
 * @file syscall.h
 * @brief 系统调用号和包装函数
 */

#ifndef _XNIX_SYSCALL_H
#define _XNIX_SYSCALL_H

#include <stdint.h>
#include <xnix/abi/capability.h>
#include <xnix/abi/syscall.h>

/*
 * 系统调用内联包装（x86 调用约定）
 */
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

static inline int syscall4(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4));
    return ret;
}

static inline int syscall5(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4,
                           uint32_t arg5) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4), "D"(arg5));
    return ret;
}

/*
 * 高层系统调用包装
 */
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

static inline void sys_sleep(uint32_t ms) {
    syscall1(SYS_SLEEP, ms);
}

static inline int sys_module_count(void) {
    return syscall0(SYS_MODULE_COUNT);
}

static inline int sys_endpoint_create(void) {
    return syscall0(SYS_ENDPOINT_CREATE);
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

#endif /* _XNIX_SYSCALL_H */
