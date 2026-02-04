#ifndef KERNEL_IO_IOPORT_H
#define KERNEL_IO_IOPORT_H

#include <xnix/perm.h>
#include <xnix/types.h>

struct process;

/*
 * I/O 端口访问使用 Permission System.
 *
 * 权限节点格式:
 * - xnix.io.port.*          (所有端口)
 * - xnix.io.port.<port>     (单个端口)
 * - xnix.io.port.<start>-<end> (端口范围)
 *
 * 检查函数:perm_check_ioport(proc, port)
 */

/**
 * @brief I/O 子系统初始化
 */
void ioport_init(void);

#endif
