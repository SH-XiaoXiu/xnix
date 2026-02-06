/**
 * @file driver/irq.h
 * @brief IRQ 管理辅助函数
 *
 * 提供内联包装器用于 IRQ 绑定和等待.
 * 基于权限检查.
 */

#ifndef XNIX_DRIVER_IRQ_H
#define XNIX_DRIVER_IRQ_H

#include <stdint.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/irq.h>
#include <xnix/abi/syscall.h>

/**
 * 绑定 IRQ 到通知 handle
 *
 * @param irq    IRQ 号
 * @param handle 通知 handle (HANDLE_NOTIFICATION)
 * @return 0 成功, -EPERM 权限不足, -EBUSY 已被占用
 *
 * 需要权限: xnix.irq.<irq>
 */
static inline int irq_bind(uint8_t irq, handle_t handle) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_IRQ_BIND), "b"(irq), "c"(handle) : "memory");
    return ret;
}

/**
 * 解绑 IRQ
 *
 * @param irq IRQ 号
 * @return 0 成功, -EINVAL IRQ 号无效
 */
static inline int irq_unbind(uint8_t irq) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_IRQ_UNBIND), "b"(irq) : "memory");
    return ret;
}

/**
 * 等待 IRQ 通知
 *
 * @param handle 通知 handle
 * @return IRQ 号(正数)或 -EINTR
 */
static inline int irq_wait(handle_t handle) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_IRQ_WAIT), "b"(handle) : "memory");
    return ret;
}

/**
 * 读取 IRQ 相关数据(如键盘扫描码)
 *
 * @param irq   IRQ 号
 * @param buf   缓冲区
 * @param size  缓冲区大小
 * @param flags 标志(预留)
 * @return 读取字节数或负数错误码
 */
static inline int irq_read(uint8_t irq, void *buf, uint32_t size, uint32_t flags) {
    int ret;
    asm volatile("int $0x80"
                 : "=a"(ret)
                 : "a"(SYS_IRQ_READ), "b"(irq), "c"(buf), "d"(size), "S"(flags)
                 : "memory");
    return ret;
}

#endif /* XNIX_DRIVER_IRQ_H */
