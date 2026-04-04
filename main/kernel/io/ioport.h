#ifndef KERNEL_IO_IOPORT_H
#define KERNEL_IO_IOPORT_H

#include <xnix/cap.h>
#include <xnix/types.h>

struct process;

/*
 * I/O 端口访问使用 Capability System.
 *
 * 进程需要 CAP_IO_PORT 能力 + ioport_bitmap 中对应位.
 * 检查函数: cap_check_ioport(proc, port)
 */

/**
 * @brief I/O 子系统初始化
 */
void ioport_init(void);

#endif
