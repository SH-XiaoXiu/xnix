/**
 * @file abi/syscall.h
 * @brief 系统调用号定义
 *
 * ABI 的核心部分.
 * 编号不可更改,只能新增.
 */

#ifndef XNIX_ABI_SYSCALL_H
#define XNIX_ABI_SYSCALL_H

/*
 * 系统调用号
 *
 * 约定:
 *   - 0 保留(无效调用)
 *   - 1-99: 基础系统调用
 *   - 100-199: IPC 相关
 *   - 200-299: 内存管理
 *   - 300-399: 进程/线程管理
 */

/* 内存管理(200-209) */
#define SYS_SBRK 200 /* 调整堆大小: ebx=increment, 返回旧堆顶或 -1 */

/* 基础系统调用 */
#define SYS_PUTC         1  /* 输出字符: ebx=char */
#define SYS_EXIT         2  /* 退出进程: ebx=exit_code */
#define SYS_SLEEP        10 /* 睡眠: ebx=ms */
#define SYS_MODULE_COUNT 12 /* 获取模块数量 */
#define SYS_WRITE        13 /* 写文件描述符: ebx=fd, ecx=buf, edx=len */

/* IPC 系统调用 */
#define SYS_ENDPOINT_CREATE 3 /* 创建 endpoint */
#define SYS_IPC_SEND        4 /* 发送消息 */
#define SYS_IPC_RECV        5 /* 接收消息 */
#define SYS_IPC_CALL        6 /* RPC 调用 */
#define SYS_IPC_REPLY       7 /* RPC 回复 */

/* I/O 端口访问(需要 IOPORT capability) */
#define SYS_IOPORT_OUTB 8 /* 写端口: ebx=cap, ecx=port, edx=val */
#define SYS_IOPORT_INB  9 /* 读端口: ebx=cap, ecx=port */

/* 进程管理 */
#define SYS_SPAWN 11 /* 创建进程: ebx=spawn_args* */

/* 线程管理(300-309) */
#define SYS_THREAD_CREATE 300 /* 创建用户线程: ebx=entry, ecx=arg, edx=stack_top */
#define SYS_THREAD_EXIT   301 /* 退出当前线程: ebx=retval */
#define SYS_THREAD_JOIN   302 /* 等待线程退出: ebx=tid, ecx=retval_ptr */
#define SYS_THREAD_SELF   303 /* 获取当前 tid */
#define SYS_THREAD_YIELD  304 /* 主动让出 CPU */
#define SYS_THREAD_DETACH 305 /* 分离线程: ebx=tid */

/* 进程管理(320-329) */
#define SYS_WAITPID 320 /* 等待子进程: ebx=pid, ecx=status_ptr, edx=options */
#define SYS_GETPID  321 /* 获取当前进程 PID */
#define SYS_GETPPID 322 /* 获取父进程 PID */
#define SYS_KILL    323 /* 发送信号: ebx=pid, ecx=sig */

/* 同步原语(310-319) */
#define SYS_MUTEX_CREATE  310 /* 创建互斥锁 */
#define SYS_MUTEX_DESTROY 311 /* 销毁互斥锁: ebx=handle */
#define SYS_MUTEX_LOCK    312 /* 获取互斥锁: ebx=handle */
#define SYS_MUTEX_UNLOCK  313 /* 释放互斥锁: ebx=handle */

/* IRQ 绑定(50-59) */
#define SYS_IRQ_BIND   50 /* 绑定 IRQ: ebx=irq, ecx=notif_handle, edx=bits */
#define SYS_IRQ_UNBIND 51 /* 解除绑定: ebx=irq */
#define SYS_IRQ_READ   52 /* 读取数据: ebx=irq, ecx=buf, edx=size, esi=flags */

/* 输入队列(60-69) */
#define SYS_INPUT_WRITE    60 /* 写入字符: ebx=char */
#define SYS_INPUT_READ     61 /* 读取字符(阻塞) */
#define SYS_SET_FOREGROUND 62 /* 设置前台进程: ebx=pid */

/* VFS 系统调用(400-419) */
#define SYS_OPEN    400 /* 打开文件: ebx=path, ecx=flags */
#define SYS_CLOSE   401 /* 关闭文件: ebx=fd */
#define SYS_READ    402 /* 读取文件: ebx=fd, ecx=buf, edx=size */
#define SYS_WRITE2  403 /* VFS 写文件: ebx=fd, ecx=buf, edx=size */
#define SYS_LSEEK   404 /* 调整偏移: ebx=fd, ecx=offset, edx=whence */
#define SYS_INFO    405 /* 文件信息(路径): ebx=path, ecx=info */
#define SYS_FINFO   406 /* 文件信息(fd): ebx=fd, ecx=info */
#define SYS_OPENDIR 407 /* 打开目录: ebx=path */
#define SYS_READDIR 408 /* 读取目录项: ebx=fd, ecx=index, edx=entry */
#define SYS_MKDIR   410 /* 创建目录: ebx=path */
#define SYS_DEL     411 /* 删除文件: ebx=path */
#define SYS_MOUNT   412 /* 挂载: ebx=path, ecx=fs_ep */
#define SYS_UMOUNT  413 /* 卸载: ebx=path */

/*
 * 系统调用调用约定 (x86)
 *
 * 入口:int 0x80
 * 参数:eax=syscall_no, ebx=arg1, ecx=arg2, edx=arg3, esi=arg4, edi=arg5
 * 返回:eax=return_value (负数表示错误)
 */

#endif /* XNIX_ABI_SYSCALL_H */
