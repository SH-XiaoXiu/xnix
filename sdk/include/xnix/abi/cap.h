/**
 * @file cap.h
 * @brief 能力 (Capability) ABI 定义
 *
 * 内核强制的能力位图. 每个进程持有一个 uint32_t cap_mask,
 * 每 bit 对应一个内核级能力. 其他"权限"靠 handle 控制:
 * 没有 handle 就访问不了对应的服务.
 *
 * 内核只管硬件/CPU 特权相关的能力, 不做字符串匹配,
 * 不做 Profile, 不做通配符.
 */

#ifndef XNIX_ABI_CAP_H
#define XNIX_ABI_CAP_H

#include <xnix/abi/stdint.h>

/*
 * 能力位定义
 *
 * 每 bit 一个内核强制能力. spawn 时内核保证:
 *   child_caps & parent_caps == child_caps
 * 即子进程能力不能超过父进程.
 */
#define CAP_IPC_SEND      (1u << 0)  /* IPC 发送 */
#define CAP_IPC_RECV      (1u << 1)  /* IPC 接收 */
#define CAP_IPC_ENDPOINT  (1u << 2)  /* 创建 IPC endpoint */
#define CAP_PROCESS_EXEC  (1u << 3)  /* spawn 子进程 */
#define CAP_HANDLE_GRANT  (1u << 4)  /* 传递 handle 给其他进程 */
#define CAP_MM_MMAP       (1u << 5)  /* 映射物理内存 */
#define CAP_IO_PORT       (1u << 6)  /* 访问 IO 端口 (配合 ioport bitmap) */
#define CAP_IRQ           (1u << 7)  /* 注册硬件中断 (配合 irq_mask) */
#define CAP_DEBUG_CONSOLE (1u << 8)  /* 内核调试输出 */
#define CAP_KERNEL_KMSG   (1u << 9)  /* 读内核日志 */
#define CAP_CAP_DELEGATE  (1u << 10) /* 委托能力给其他进程 */

#define CAP_ALL           0xFFFFFFFF /* root: 拥有一切 */

/*
 * 常用能力组合 (用户态 init 用, 内核不感知)
 */
#define CAP_IPC (CAP_IPC_SEND | CAP_IPC_RECV)

/*
 * Spawn 时传递的能力描述
 */
#define SPAWN_IOPORT_RANGES_MAX 8
#define SPAWN_IRQS_MAX          16

struct spawn_caps {
    uint32_t cap_mask;                                            /* 能力位图 */
    struct { uint16_t start, end; } ioports[SPAWN_IOPORT_RANGES_MAX]; /* IO 端口范围 */
    uint8_t  ioport_count;                                        /* IO 端口范围数量 */
    uint8_t  irqs[SPAWN_IRQS_MAX];                                /* 允许的 IRQ 号 */
    uint8_t  irq_count;                                           /* IRQ 数量 */
    uint8_t  _pad;
};

#endif /* XNIX_ABI_CAP_H */
