/**
 * @file process_internal.h
 * @brief 进程管理内部 API
 *
 * 此文件仅供 kernel/process/ 和 kernel/sys/ 内部使用,包含进程管理私有实现细节.
 * 跨子系统 API 见 <xnix/process_def.h>
 */

#ifndef KERNEL_PROCESS_INTERNAL_H
#define KERNEL_PROCESS_INTERNAL_H

#include <xnix/process_def.h>

/* 内部变量(跨文件共享) */
extern struct process *process_list;
extern spinlock_t      process_list_lock;
extern struct process  kernel_process;

/* 内部函数 */
void free_pid(pid_t pid);

#endif
