/**
 * @file abi/syscall.h
 * @brief 系统调用号定义(Handle + Capability 模型)
 *
 * 重新组织的系统调用分类,基于微内核机制设计.
 */

#ifndef XNIX_ABI_SYSCALL_H
#define XNIX_ABI_SYSCALL_H

/*
 * 系统调用分类(使用清晰的编号范围)
 *
 * 设计原则:
 *   - 内核只提供机制(IPC, 调度, 内存, 句柄, 能力)
 *   - 策略由用户态服务实现
 *   - Handle 和 Capability 分离检查
 *   - I/O 端口访问基于能力位(无需 handle)
 */

/* IPC (100-119) */
#define SYS_ENDPOINT_CREATE 100 /* 创建 IPC 端点, 返回 handle */
#define SYS_IPC_SEND        101 /* 发送消息: ebx=handle, ecx=msg* */
#define SYS_IPC_RECV        102 /* 接收消息: ebx=handle, ecx=msg* */
#define SYS_IPC_CALL        103 /* RPC 调用: ebx=handle, ecx=msg* */
#define SYS_IPC_REPLY       104 /* RPC 回复: ecx=msg* */
#define SYS_IPC_REPLY_TO    105 /* 延迟回复: ebx=sender_tid, ecx=msg* */
#define SYS_IPC_WAIT_ANY    106 /* 等待多个对象: ebx=wait_set*, ecx=timeout_ms */

/* Pipe (110-119) — 字节流通道 */
#define SYS_PIPE_CREATE     110 /* 创建管道: ebx=read_h*, ecx=write_h* */
#define SYS_PIPE_READ       111 /* 读管道: ebx=handle, ecx=buf, edx=size */
#define SYS_PIPE_WRITE      112 /* 写管道: ebx=handle, ecx=buf, edx=size */

/* 内存管理 (200-219) */
#define SYS_SBRK 200 /* 堆管理: ebx=increment, 返回旧堆顶或 -1 */
#define SYS_MMAP_PHYS \
    201 /* 映射物理内存: ebx=handle, ecx=offset, edx=size, esi=prot, edi=out_size */
#define SYS_MUNMAP       202 /* 取消映射: ebx=addr, ecx=size */
#define SYS_PHYSMEM_INFO 203 /* 查询物理内存信息: ebx=handle, ecx=info_ptr */
#define SYS_SHM_CREATE   204 /* 创建匿名共享内存: ebx=size, 返回 handle */

/* 任务/线程 (300-319) */
#define SYS_THREAD_CREATE 301 /* 创建用户线程: ebx=entry, ecx=arg, edx=stack_top */
#define SYS_THREAD_EXIT   302 /* 退出线程: ebx=retval */
#define SYS_THREAD_JOIN   303 /* 等待线程: ebx=tid, ecx=retval_ptr */
#define SYS_THREAD_YIELD  304 /* 主动让出 CPU */
#define SYS_EXIT          305 /* 退出进程: ebx=exit_code */
#define SYS_THREAD_SELF   306 /* 获取当前 tid */
#define SYS_THREAD_DETACH 307 /* 分离线程: ebx=tid */

/* Handle 管理 (400-419) */
#define SYS_HANDLE_FIND      400 /* 查找命名 handle: ebx=name, 返回 handle 或 -1 */
#define SYS_HANDLE_GRANT     401 /* 授予 handle: ebx=pid, ecx=handle, edx=name, esi=rights */
#define SYS_HANDLE_CLOSE     402 /* 关闭 handle: ebx=handle */
#define SYS_HANDLE_DUPLICATE 403 /* 复制 handle: ebx=src, ecx=dst_hint, edx=name */
#define SYS_HANDLE_LIST      404 /* 列出 handle: ebx=buf, ecx=max_count, 返回条目数 */

/* 能力 (420-439) */
#define SYS_CAP_CHECK  420 /* 检查能力: ebx=cap_bit */
#define SYS_CAP_GRANT  422 /* 委托能力: ebx=pid, ecx=cap_bits */
#define SYS_CAP_REVOKE 423 /* 撤销能力: ebx=pid, ecx=cap_bits */
#define SYS_CAP_QUERY  424 /* 查询能力: 返回当前进程 cap_mask */

/* 硬件访问 (500-519) - 基于能力位 */
#define SYS_IOPORT_OUTB 500 /* 写端口 8位: ebx=port, ecx=val (需 CAP_IO_PORT) */
#define SYS_IOPORT_INB  501 /* 读端口 8位: ebx=port */
#define SYS_IOPORT_OUTW 502 /* 写端口 16位: ebx=port, ecx=val */
#define SYS_IOPORT_INW  503 /* 读端口 16位: ebx=port */
#define SYS_IRQ_BIND    504 /* 绑定 IRQ: ebx=irq, ecx=notif_handle (需 CAP_IRQ) */
#define SYS_IRQ_UNBIND  505 /* 解绑 IRQ: ebx=irq */
#define SYS_IRQ_WAIT    506 /* 等待 IRQ: ebx=notif_handle */
#define SYS_IRQ_READ    507 /* 读取 IRQ 数据: ebx=irq, ecx=buf, edx=size, esi=flags */

/* 进程管理 (600-619) */
#define SYS_GETPID         600 /* 获取当前进程 PID */
#define SYS_WAITPID        601 /* 等待子进程: ebx=pid, ecx=status*, edx=options */
#define SYS_KILL           602 /* 发送信号: ebx=pid, ecx=sig */
#define SYS_SET_FOREGROUND 603 /* 设置前台进程: ebx=pid */
#define SYS_GETPPID        604 /* 获取父进程 PID */
#define SYS_EXEC           605 /* 执行新程序: ebx=exec_args* */
#define SYS_PROCLIST       606 /* 获取进程列表: ebx=proclist_args* */
#define SYS_SETPGID        607 /* 设置进程组: ebx=pid(0=self), ecx=pgid(0=pid) */
#define SYS_GETPGID        608 /* 获取进程组: ebx=pid(0=self) */
#define SYS_PROC_WATCH     609 /* 观察进程退出: ebx=pid, ecx=notif_handle, edx=bits */

/* 同步原语 (700-719) */
#define SYS_MUTEX_CREATE  700 /* 创建互斥锁 */
#define SYS_MUTEX_LOCK    701 /* 获取锁: ebx=handle */
#define SYS_MUTEX_UNLOCK  702 /* 释放锁: ebx=handle */
#define SYS_MUTEX_DESTROY 703 /* 销毁锁: ebx=handle */

/* 通知/信号 (800-819) */
#define SYS_EVENT_CREATE 800 /* 创建事件, 返回 handle */
#define SYS_EVENT_WAIT   801 /* 等待事件: ebx=handle */
#define SYS_EVENT_SIGNAL 802 /* 发送事件: ebx=handle, ecx=bits */

/* 内核日志 (850-859) */
#define SYS_KMSG_READ 850 /* 读取内核日志: ebx=seq_ptr, ecx=buf, edx=size */

/* 杂项 (900-999) */
#define SYS_SLEEP             900 /* 睡眠: ebx=ms */
#define SYS_DEBUG_WRITE       901 /* 调试控制台写(仅编译时启用): ebx=buf_ptr, ecx=len */
#define SYS_DEBUG_SET_COLOR   902 /* 调试控制台颜色: ebx=fg, ecx=bg (仅编译时启用) */
#define SYS_DEBUG_RESET_COLOR 903 /* 调试控制台颜色复位(仅编译时启用) */
#define SYS_DEBUG_READ        904 /* 调试控制台读(仅编译时启用): ebx=buf_ptr, ecx=len */

/*
 * 系统调用调用约定(x86)
 *
 * 入口:int 0x80
 * 参数:eax=syscall_no, ebx=arg1, ecx=arg2, edx=arg3, esi=arg4, edi=arg5
 * 返回:eax=return_value(负数表示错误,见 errno.h)
 */

#endif /* XNIX_ABI_SYSCALL_H */
