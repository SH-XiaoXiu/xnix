/**
 * @file abi/syscall.h
 * @brief 系统调用号定义(Handle + Permission 模型)
 *
 * 重新组织的系统调用分类,基于微内核机制设计.
 */

#ifndef XNIX_ABI_SYSCALL_H
#define XNIX_ABI_SYSCALL_H

/*
 * 系统调用分类(使用清晰的编号范围)
 *
 * 设计原则:
 *   - 内核只提供机制(IPC, 调度, 内存, 句柄, 权限)
 *   - 策略由用户态服务实现
 *   - Handle 和 Permission 分离检查
 *   - I/O 端口访问基于权限(无需 handle)
 */

/* IPC (100-119) */
#define SYS_ENDPOINT_CREATE 100 /* 创建 IPC 端点, 返回 handle */
#define SYS_IPC_SEND        101 /* 发送消息: ebx=handle, ecx=msg* */
#define SYS_IPC_RECV        102 /* 接收消息: ebx=handle, ecx=msg* */
#define SYS_IPC_CALL        103 /* RPC 调用: ebx=handle, ecx=msg* */
#define SYS_IPC_REPLY       104 /* RPC 回复: ecx=msg* */
#define SYS_IPC_REPLY_TO    105 /* 延迟回复: ebx=sender_tid, ecx=msg* */

/* 内存管理 (200-219) */
#define SYS_SBRK        200 /* 堆管理: ebx=increment, 返回旧堆顶或 -1 */
#define SYS_MMAP_PHYS   201 /* 映射物理内存: ebx=handle, ecx=offset, edx=size, esi=prot, edi=out_size */
#define SYS_MUNMAP      202 /* 取消映射: ebx=addr, ecx=size */
#define SYS_PHYSMEM_INFO 203 /* 查询物理内存信息: ebx=handle, ecx=info_ptr */

/* 任务/线程 (300-319) */
#define SYS_SPAWN         300 /* 创建进程: ebx=spawn_args* */
#define SYS_THREAD_CREATE 301 /* 创建用户线程: ebx=entry, ecx=arg, edx=stack_top */
#define SYS_THREAD_EXIT   302 /* 退出线程: ebx=retval */
#define SYS_THREAD_JOIN   303 /* 等待线程: ebx=tid, ecx=retval_ptr */
#define SYS_THREAD_YIELD  304 /* 主动让出 CPU */
#define SYS_EXIT          305 /* 退出进程: ebx=exit_code */
#define SYS_THREAD_SELF   306 /* 获取当前 tid */
#define SYS_THREAD_DETACH 307 /* 分离线程: ebx=tid */

/* Handle 管理 (400-419) */
#define SYS_HANDLE_FIND      400 /* 查找命名 handle: ebx=name, 返回 handle 或 -1 */
#define SYS_HANDLE_GRANT     401 /* 授予 handle: ebx=pid, ecx=handle, edx=name */
#define SYS_HANDLE_CLOSE     402 /* 关闭 handle: ebx=handle */
#define SYS_HANDLE_DUPLICATE 403 /* 复制 handle: ebx=src, ecx=dst_hint, edx=name */

/* 权限 (420-439) */
#define SYS_PERM_CHECK 20 /* 检查权限: ebx=perm_id(编号待迁移) */

/* 硬件访问 (500-519) - 基于权限 */
#define SYS_IOPORT_OUTB 500 /* 写端口 8位: ebx=port, ecx=val (需 xnix.io.port.<port> 权限) */
#define SYS_IOPORT_INB  501 /* 读端口 8位: ebx=port */
#define SYS_IOPORT_OUTW 502 /* 写端口 16位: ebx=port, ecx=val */
#define SYS_IOPORT_INW  503 /* 读端口 16位: ebx=port */
#define SYS_IRQ_BIND    504 /* 绑定 IRQ: ebx=irq, ecx=notif_handle (需 xnix.irq.<n> 权限) */
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

/* 同步原语 (700-719) */
#define SYS_MUTEX_CREATE  700 /* 创建互斥锁 */
#define SYS_MUTEX_LOCK    701 /* 获取锁: ebx=handle */
#define SYS_MUTEX_UNLOCK  702 /* 释放锁: ebx=handle */
#define SYS_MUTEX_DESTROY 703 /* 销毁锁: ebx=handle */

/* 通知/信号 (800-819) */
#define SYS_NOTIFICATION_CREATE 800 /* 创建通知, 返回 handle */
#define SYS_NOTIFICATION_WAIT   801 /* 等待通知: ebx=handle */

/* 杂项 (900-999) */
#define SYS_SLEEP     900 /* 睡眠: ebx=ms */
#define SYS_DEBUG_PUT 901 /* 调试输出(仅编译时启用): ebx=char */

/*
 * 系统调用调用约定(x86)
 *
 * 入口:int 0x80
 * 参数:eax=syscall_no, ebx=arg1, ecx=arg2, edx=arg3, esi=arg4, edi=arg5
 * 返回:eax=return_value(负数表示错误,见 errno.h)
 */

#endif /* XNIX_ABI_SYSCALL_H */
