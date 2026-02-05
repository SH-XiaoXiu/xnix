/**
 * @file driver/ioport.h
 * @brief I/O 端口访问辅助函数
 *
 * 提供内联包装器用于 I/O 端口访问系统调用.
 * 基于权限检查,无需 handle.
 */

#ifndef XNIX_DRIVER_IOPORT_H
#define XNIX_DRIVER_IOPORT_H

#include <stdint.h>
#include <xnix/abi/syscall.h>

/**
 * 写入 8 位数据到 I/O 端口
 *
 * @param port 端口号
 * @param val  要写入的值
 * @return 0 成功, -EPERM 权限不足
 *
 * 需要权限: xnix.io.port.<port>
 */
static inline int ioport_outb(uint16_t port, uint8_t val) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_IOPORT_OUTB), "b"(port), "c"(val) : "memory");
    return ret;
}

/**
 * 从 I/O 端口读取 8 位数据
 *
 * @param port 端口号
 * @return 读取的值(正数)或 -EPERM
 *
 * 需要权限: xnix.io.port.<port>
 */
static inline int ioport_inb(uint16_t port) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_IOPORT_INB), "b"(port) : "memory");
    return ret;
}

/**
 * 写入 16 位数据到 I/O 端口
 *
 * @param port 端口号
 * @param val  要写入的值
 * @return 0 成功, -EPERM 权限不足
 */
static inline int ioport_outw(uint16_t port, uint16_t val) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_IOPORT_OUTW), "b"(port), "c"(val) : "memory");
    return ret;
}

/**
 * 从 I/O 端口读取 16 位数据
 *
 * @param port 端口号
 * @return 读取的值(正数)或 -EPERM
 */
static inline int ioport_inw(uint16_t port) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(SYS_IOPORT_INW), "b"(port) : "memory");
    return ret;
}

#endif /* XNIX_DRIVER_IOPORT_H */
