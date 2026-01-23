/**
 * @file sched.h
 * @brief 调度器内部头文件（私有）
 */

#ifndef KERNEL_SCHED_PRIVATE_H
#define KERNEL_SCHED_PRIVATE_H

#include <arch/smp.h>

#include <xnix/thread.h>
#include <xnix/types.h>

/*
 * 调度策略接口
 * 机制与策略分离：
 *   机制 (scheduler)：何时调度、如何切换上下文
 *   策略 (policy)：选哪个线程、如何管理队列
 */
struct sched_policy {
    const char *name;

    /* 初始化策略 */
    void (*init)(void);

    /* 线程就绪，加入运行队列 */
    void (*enqueue)(struct thread *t, cpu_id_t cpu);

    /* 线程移出运行队列 */
    void (*dequeue)(struct thread *t);

    /* 选择下一个要运行的线程（当前 CPU） */
    struct thread *(*pick_next)(void);

    /* 时钟中断处理，返回是否需要重新调度 */
    bool (*tick)(struct thread *current);

    /* 选择最适合的 CPU（负载均衡） */
    cpu_id_t (*select_cpu)(struct thread *t);
};

/*
 * Per-CPU 运行队列
 **/

struct runqueue {
    struct thread *head;       /* 就绪队列头 */
    struct thread *tail;       /* 就绪队列尾 */
    struct thread *current;    /* 当前运行线程 */
    uint32_t       nr_running; /* 运行队列长度（负载） */
};

/*
 * 内部函数
 **/

/**
 * 获取当前 CPU 的运行队列
 */
struct runqueue *sched_get_runqueue(cpu_id_t cpu);

/**
 * 设置调度策略
 */
void sched_set_policy(struct sched_policy *policy);

/**
 * 创建线程并加入调度（内部使用）
 */
struct thread *sched_spawn(const char *name, void (*entry)(void *), void *arg);

/**
 * 执行调度（切换到下一个线程）
 */
void schedule(void);

/*
 * 内置策略声明
 **/

/* Round-Robin 轮转调度 */
extern struct sched_policy sched_policy_rr;

#endif
