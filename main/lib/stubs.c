/**
 * @file stubs.c
 * @brief 可选子系统的弱符号空实现
 *
 * 当子系统被编译时,强符号会覆盖这些弱符号.
 * 当子系统被禁用时,调用方链接到这些空实现,无需条件编译.
 */

#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/ipc.h>
#include <xnix/types.h>

/*
 * IPC 子系统空实现
 */

__attribute__((weak)) void ipc_init(void) {
    /* 空实现 */
}

__attribute__((weak)) int sys_endpoint_create(const char *name) {
    (void)name;
    return HANDLE_INVALID;
}

__attribute__((weak)) int ipc_send(handle_t ep_handle, struct ipc_message *msg,
                                   uint32_t timeout_ms) {
    (void)ep_handle;
    (void)msg;
    (void)timeout_ms;
    return -ENOSYS;
}

__attribute__((weak)) int ipc_receive(handle_t ep_handle, struct ipc_message *msg,
                                      uint32_t timeout_ms) {
    (void)ep_handle;
    (void)msg;
    (void)timeout_ms;
    return -ENOSYS;
}

__attribute__((weak)) int ipc_call(handle_t ep_handle, struct ipc_message *request,
                                   struct ipc_message *reply, uint32_t timeout_ms) {
    (void)ep_handle;
    (void)request;
    (void)reply;
    (void)timeout_ms;
    return -ENOSYS;
}

__attribute__((weak)) int ipc_reply(struct ipc_message *reply) {
    (void)reply;
    return -ENOSYS;
}

__attribute__((weak)) int ipc_send_async(handle_t ep_handle, struct ipc_message *msg) {
    (void)ep_handle;
    (void)msg;
    return -ENOSYS;
}

__attribute__((weak)) handle_t ipc_wait_any(struct ipc_wait_set *set, uint32_t timeout_ms) {
    (void)set;
    (void)timeout_ms;
    return HANDLE_INVALID;
}

__attribute__((weak)) int sys_notification_create(const char *name) {
    (void)name;
    return HANDLE_INVALID;
}

__attribute__((weak)) void notification_signal(handle_t notif_handle, uint32_t bits) {
    (void)notif_handle;
    (void)bits;
}

__attribute__((weak)) uint32_t notification_wait(handle_t notif_handle) {
    (void)notif_handle;
    return 0;
}

/* VFS moved to userspace - no kernel stubs needed */

/*
 * Handle 子系统空实现
 */

__attribute__((weak)) int sys_handle_close(handle_t handle) {
    (void)handle;
    return -ENOSYS;
}

__attribute__((weak)) handle_t sys_handle_duplicate(handle_t handle) {
    (void)handle;
    return HANDLE_INVALID;
}

/*
 * I/O Port 子系统空实现
 */

__attribute__((weak)) void ioport_init(void) {
    /* 空实现 */
}

/*
 * Input 子系统空实现
 */

__attribute__((weak)) void input_init(void) {
    /* 空实现 */
}
