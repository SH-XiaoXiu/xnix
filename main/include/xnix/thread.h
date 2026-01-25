/**
 * @file thread.h
 * @brief 线程 API
 *
 * 线程是内核最小调度单位,一个可暂停和恢复的执行流.
 * 完整定义见 <kernel/sched/sched.h>
 */

#ifndef XNIX_THREAD_H
#define XNIX_THREAD_H

#include <xnix/types.h>

/**
 * 线程句柄
 *
 * 用访问器获取信息:thread_get_tid(),thread_get_name(),thread_get_state()
 */
typedef struct thread *thread_t;

typedef uint32_t tid_t;
#define TID_INVALID ((tid_t) - 1)

/**
 * 线程状态
 */
typedef enum {
    THREAD_READY,   /* 就绪,等待调度 */
    THREAD_RUNNING, /* 正在执行 */
    THREAD_BLOCKED, /* 阻塞,等待事件 */
    THREAD_EXITED,  /* 已退出,待回收 */
} thread_state_t;

/**
 * 创建线程
 *
 * @param name  线程名,用于调试,不会复制
 * @param entry 入口函数
 * @param arg   传给入口函数的参数
 * @return 线程句柄,失败返回 NULL
 */
thread_t thread_create(const char *name, void (*entry)(void *), void *arg);

/**
 * 终止当前线程,不返回
 */
void thread_exit(int code) __attribute__((noreturn));

/**
 * 主动让出 CPU
 */
void thread_yield(void);

/**
 * 获取当前线程
 */
thread_t thread_current(void);

/* 访问器 */
tid_t          thread_get_tid(thread_t t);
const char    *thread_get_name(thread_t t);
thread_state_t thread_get_state(thread_t t);

/**
 * 睡眠
 *
 * @param ms    毫秒数
 * @param ticks tick 数(1 tick = 1000/CFG_SCHED_HZ ms)
 */
void sleep_ms(uint32_t ms);
void sleep_ticks(uint32_t ticks);

/**
 * 调度器接口
 *
 * sched_init  初始化调度器
 * sched_tick  时钟中断回调,由定时器驱动调用
 * sched_block 阻塞当前线程,wait_chan 是等待的对象地址
 * sched_wakeup 唤醒等待 wait_chan 的所有线程
 */
void sched_init(void);
void sched_tick(void);
void sched_block(void *wait_chan);
void sched_wakeup(void *wait_chan);

#endif
