/**
 * @file signal.h
 * @brief 信号定义(用户态)
 */

#ifndef _SIGNAL_H
#define _SIGNAL_H

/* 信号编号 */
#define SIGHUP  1  /* 终端挂起 */
#define SIGINT  2  /* 中断(Ctrl+C) */
#define SIGQUIT 3  /* 退出(Ctrl+\) */
#define SIGILL  4  /* 非法指令 */
#define SIGTRAP 5  /* 断点 */
#define SIGABRT 6  /* 中止 */
#define SIGBUS  7  /* 总线错误 */
#define SIGFPE  8  /* 浮点异常 */
#define SIGKILL 9  /* 强制终止(不可捕获) */
#define SIGSEGV 11 /* 段错误 */
#define SIGPIPE 13 /* 管道断开 */
#define SIGALRM 14 /* 定时器 */
#define SIGTERM 15 /* 终止请求 */
#define SIGCHLD 17 /* 子进程状态改变 */
#define SIGCONT 18 /* 继续执行 */
#define SIGSTOP 19 /* 停止(不可捕获) */
#define SIGTSTP 20 /* 终端停止(Ctrl+Z) */

#endif /* _SIGNAL_H */
