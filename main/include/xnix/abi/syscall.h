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
#define SYS_SBRK       200 /* 调整堆大小: ebx=increment, 返回旧堆顶或 -1 */
#define SYS_FB_INFO    201 /* 获取 framebuffer 信息: ebx=info*, 返回 0 成功 -1 失败 */
#define SYS_FB_MAP     202 /* 映射 framebuffer: 返回用户空间地址或 -1 */
#define SYS_MODULE_MAP 203 /* 映射 boot module: ebx=index, ecx=size_out*, 返回用户空间地址或 -1 */

/* 基础系统调用 */
#define SYS_EXIT         2  /* 退出进程: ebx=exit_code */
#define SYS_SLEEP        10 /* 睡眠: ebx=ms */
#define SYS_MODULE_COUNT 12 /* 获取模块数量 */
#define SYS_WRITE        13 /* 写stdout/stderr: ebx=fd, ecx=buf, edx=len */

/* Handle 系统调用 */
#define SYS_HANDLE_CLOSE     18 /* 关闭句柄: ebx=handle */
#define SYS_HANDLE_DUPLICATE 19 /* 复制句柄: ebx=src, ecx=dst_hint, edx=name */
#define SYS_PERM_CHECK       20 /* 检查权限: ebx=perm_id */
#define SYS_HANDLE_FIND      21 /* 查找命名 handle: ebx=name */

/* IPC 系统调用 */
#define SYS_ENDPOINT_CREATE 3  /* 创建 endpoint */
#define SYS_IPC_SEND        4  /* 发送消息 */
#define SYS_IPC_RECV        5  /* 接收消息 */
#define SYS_IPC_CALL        6  /* RPC 调用 */
#define SYS_IPC_REPLY       7  /* RPC 回复 */
#define SYS_IPC_REPLY_TO    17 /* 延迟回复: ebx=sender_tid, ecx=reply_msg */

/* I/O 端口访问(需要 IOPORT 权限) */
#define SYS_IOPORT_OUTB 8  /* 写端口(8位): ebx=port, ecx=val */
#define SYS_IOPORT_INB  9  /* 读端口(8位): ebx=port */
#define SYS_IOPORT_OUTW 14 /* 写端口(16位): ebx=port, ecx=val */
#define SYS_IOPORT_INW  15 /* 读端口(16位): ebx=port */
/* #define SYS_IOPORT_CREATE_RANGE 16 废弃 */

/* 进程管理 */
#define SYS_SPAWN 11 /* 创建进程: ebx=spawn_args* (from module) */

/* 线程管理(300-309) */
#define SYS_THREAD_CREATE 300 /* 创建用户线程: ebx=entry, ecx=arg, edx=stack_top */
#define SYS_THREAD_EXIT   301 /* 退出当前线程: ebx=retval */
#define SYS_THREAD_JOIN   302 /* 等待线程退出: ebx=tid, ecx=retval_ptr */
#define SYS_THREAD_SELF   303 /* 获取当前 tid */
#define SYS_THREAD_YIELD  304 /* 主动让出 CPU */
#define SYS_THREAD_DETACH 305 /* 分离线程: ebx=tid */

/* 进程管理(320-329) */
#define SYS_WAITPID  320 /* 等待子进程: ebx=pid, ecx=status_ptr, edx=options */
#define SYS_GETPID   321 /* 获取当前进程 PID */
#define SYS_GETPPID  322 /* 获取父进程 PID */
#define SYS_KILL     323 /* 发送信号: ebx=pid, ecx=sig */
#define SYS_EXEC     324 /* 执行新程序: ebx=exec_args* */
#define SYS_PROCLIST 325 /* 获取进程列表: ebx=proclist_args* */

/* 同步原语(310-319) */
#define SYS_MUTEX_CREATE  310 /* 创建互斥锁 */
#define SYS_MUTEX_DESTROY 311 /* 销毁互斥锁: ebx=handle */
#define SYS_MUTEX_LOCK    312 /* 获取互斥锁: ebx=handle */
#define SYS_MUTEX_UNLOCK  313 /* 释放互斥锁: ebx=handle */

/* IRQ 绑定(50-59) */
#define SYS_IRQ_BIND            50 /* 绑定 IRQ: ebx=irq, ecx=notif_handle, edx=bits */
#define SYS_IRQ_UNBIND          51 /* 解除绑定: ebx=irq */
#define SYS_IRQ_READ            52 /* 读取数据: ebx=irq, ecx=buf, edx=size, esi=flags */
#define SYS_NOTIFICATION_CREATE 53 /* 创建通知: 返回 handle */
#define SYS_NOTIFICATION_WAIT   54 /* 等待通知: ebx=handle */

/*
 * 系统调用调用约定 (x86)
 *
 * 入口:int 0x80
 * 参数:eax=syscall_no, ebx=arg1, ecx=arg2, edx=arg3, esi=arg4, edi=arg5
 * 返回:eax=return_value (负数表示错误)
 */

#endif /* XNIX_ABI_SYSCALL_H */
