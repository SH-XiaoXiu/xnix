/**
 * @file sched.h
 * @brief 调度器公共接口（外部模块使用）
 */

#ifndef XNIX_SCHED_H
#define XNIX_SCHED_H

#include <xnix/thread.h>

/**
 * 初始化调度器
 */
void sched_init(void);

/**
 * 创建线程并加入调度
 */
struct thread *sched_spawn(const char *name, void (*entry)(void *), void *arg);

/**
 * 主动让出 CPU（协作式调度）
 */
void sched_yield(void);

/**
 * 阻塞当前线程
 * @param wait_chan 等待通道（锁、信号量等对象的地址）
 */
void sched_block(void *wait_chan);

/**
 * 唤醒等待某个通道的所有线程
 */
void sched_wakeup(void *wait_chan);

/**
 * 时钟中断回调（由定时器驱动）
 */
void sched_tick(void);

/**
 * 获取当前线程
 */
struct thread *sched_current(void);

/**
 * 迁移线程到指定 CPU
 */
void sched_migrate(struct thread *t, cpu_id_t target_cpu);

/**
 * 标记当前线程待销毁（下次调度时释放内存）
 */
void sched_destroy_current(void);

#endif
