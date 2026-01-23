/**
 * @file thread.h
 * @brief 线程 - 内核最小调度单位
 *
 * 线程 = 可暂停/恢复的执行流（寄存器 + 栈）
 * 进程 = 资源容器（地址空间），至少包含一个线程
 * 调度器只看线程，不关心进程
 */

#ifndef XNIX_THREAD_H
#define XNIX_THREAD_H

#include <xnix/types.h>
#include <arch/smp.h>

typedef uint32_t tid_t;
#define TID_INVALID ((tid_t)-1)

/**
 * 线程状态
 * 本质是状态机
 */
typedef enum {
    THREAD_READY,   /* 就绪，等待调度 */
    THREAD_RUNNING, /* 正在执行 */
    THREAD_BLOCKED, /* 等待事件（锁、I/O、睡眠） */
    THREAD_EXITED,  /* 已退出，待回收 */
} thread_state_t;

/*
 * 线程上下文只保存 callee-saved 寄存器和栈指针，因为线程切换时调度器只关心恢复现场。
 * eax/ecx/edx 等 caller-saved 寄存器不需要调度器保存，它们的保存由函数调用约定管理，
 * 完全是编译器生成指令维护函数调用返回逻辑，无需内核干预。无论是内核函数还是用户函数，
 * 只要存在函数调用概念，这套寄存器保存和栈管理规则都是统一的，和线程调度本身无关。
 */

struct thread_context {
    uint32_t esp; /* 栈指针 - 切换核心 */
    uint32_t ebp; /* 基指针 */
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
};

/**
 * 线程控制块 (TCB)
 */
struct thread {
    tid_t       tid;
    const char *name;

    thread_state_t state;
    int            priority;    /* 小 = 高优先级 */
    uint32_t       time_slice;  /* 剩余时间片（tick 数） */

    struct thread_context ctx;
    void                 *stack; /* 栈底 */
    size_t                stack_size;

    struct process *owner; /* 所属进程，内核线程为 NULL */

    /* 多核相关 */
    uint32_t cpus_workable; /* 位图：bit N = 1 表示可在 CPU N 运行（全 1 = 任意核） */
    cpu_id_t running_on;    /* 当前运行在哪个核上（-1 表示未运行） */

    /* 调度策略 */
    struct sched_policy *policy; /* 线程专属策略（NULL 则用默认策略） */

    struct thread *next; /* 队列链接 */

    void *wait_chan; /* 阻塞在什么上 */
    int   exit_code;
};

struct process;      /* 前向声明 */
struct sched_policy; /* 前向声明 */

/* CPU 位图操作 */
#define CPUS_ALL         0xFFFFFFFF                  /* 任意核 */
#define CPUS_SET(mask, cpu)   ((mask) | (1U << (cpu)))
#define CPUS_CLEAR(mask, cpu) ((mask) & ~(1U << (cpu)))
#define CPUS_TEST(mask, cpu)  ((mask) & (1U << (cpu)))
#define CPUS_ONLY(cpu)        (1U << (cpu))          /* 仅绑定单核 */

/* 线程操作 */
struct thread *thread_create(const char *name, void (*entry)(void *), void *arg);
void           thread_exit(int code) __attribute__((noreturn));
void           thread_yield(void);
struct thread *thread_current(void);

#endif
