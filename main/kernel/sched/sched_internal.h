/**
 * @file sched_internal.h
 * @brief 调度器内部 API
 *
 * 此文件仅供 kernel/sched/ 和 kernel/sys/ 内部使用,包含调度器私有实现细节.
 * 跨子系统 API 见 <xnix/thread_def.h>
 */

#ifndef KERNEL_SCHED_INTERNAL_H
#define KERNEL_SCHED_INTERNAL_H

#include <xnix/thread_def.h>

/*
 * 内置策略声明
 **/

/* Round-Robin 轮转调度 */
extern struct sched_policy sched_policy_rr;

/*
 * 线程模块内部函数(thread.c)
 * 这些函数主要供调度器核心使用
 */

/**
 * 初始化 idle 线程(由 sched_init 调用)
 */
void thread_init_idle(void);

/**
 * 清理已退出的僵尸线程
 * 从僵尸链表中移除已经 detached 或 joined 的线程并释放资源
 */
void sched_cleanup_zombie(void);

/**
 * 获取指定 CPU 的 idle 线程
 */
struct thread *sched_get_idle_thread(cpu_id_t cpu);

/**
 * 获取指定 CPU 的 zombie 线程链表头指针
 */
struct thread **sched_get_zombie_list(cpu_id_t cpu);

/*
 * 睡眠模块(sleep.c)
 */

/**
 * 检查并唤醒睡眠到期的线程(由 sched_tick 调用)
 */
void sleep_check_wakeup(void);

/*
 * 系统统计 API (statistics.c)
 */

/**
 * 获取全局 tick 计数
 */
uint64_t sched_get_global_ticks(void);

/**
 * 获取 idle tick 计数
 */
uint64_t sched_get_idle_ticks(void);

/**
 * 增加全局 tick 计数(由 sched_tick 调用)
 */
void sched_stat_tick(void);

/**
 * 增加 idle tick 计数(由 sched_tick 调用)
 */
void sched_stat_idle_tick(void);

#endif
