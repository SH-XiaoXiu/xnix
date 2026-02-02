/**
 * @file sched.h
 * @brief 调度器完整定义
 *
 * 包含线程完整定义,调度策略,运行队列等结构.
 * 公共 API 见 <xnix/thread.h>
 */

#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H

#include <xnix/percpu.h>
#include <xnix/thread.h>
#include <xnix/types.h>

/*
 * 线程上下文只保存 callee-saved 寄存器和栈指针,因为线程切换时调度器只关心恢复现场.
 * eax/ecx/edx 等 caller-saved 寄存器不需要调度器保存,它们的保存由函数调用约定管理,
 * 完全是编译器生成指令维护函数调用返回逻辑,无需内核干预.无论是内核函数还是用户函数,
 * 只要存在函数调用概念,这套寄存器保存和栈管理规则都是统一的,和线程调度本身无关.
 */

struct thread_context {
    uint32_t esp; /* 栈指针 - 切换核心 */
    uint32_t ebp; /* 基指针 */
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
};

struct process;      /* 前向声明 */
struct sched_policy; /* 前向声明 */

/**
 * 线程控制块 (TCB)
 */
struct thread {
    tid_t       tid;
    const char *name;

    thread_state_t state;
    int            priority;   /* 小 = 高优先级 */
    uint32_t       time_slice; /* 剩余时间片(tick 数) */

    struct thread_context ctx;
    void                 *stack; /* 栈底 */
    size_t                stack_size;

    struct process *owner; /* 所属进程,内核线程为 NULL */

    /* 多核相关 */
    uint32_t cpus_workable;   /* 位图:bit N = 1 表示可在 CPU N 运行(全 1 = 任意核) */
    cpu_id_t running_on;      /* 当前运行在哪个核上(-1 表示未运行) */
    cpu_id_t migrate_target;  /* 迁移目标 CPU,CPU_ID_INVALID 表示无 */
    bool     migrate_pending; /* 是否有挂起的迁移请求 */

    /* 调度策略 */
    struct sched_policy *policy; /* 线程专属策略(NULL 则用默认策略) */

    struct thread *next;      /* 运行队列/阻塞队列链接 */
    struct thread *wait_next; /* 特定等待队列链接 (如 Notification, Mutex) */
    struct thread *proc_next; /* 进程线程链表链接 */

    void    *wait_chan;   /* 阻塞在什么上 */
    uint64_t wakeup_tick; /* 睡眠唤醒时间(0 表示不在睡眠) */
    int      exit_code;

    /* IPC 状态 */
    /* 一个线程在同一时刻只允许一个 IPC 操作:
     * ipc_req_msg   - 当前线程正在发送的消息(send/call),内核保证一次 IPC 操作完成前不会覆盖
     * ipc_reply_msg - 当前线程等待接收的回复(call/receive),内核保证在消息处理完成前不会被覆盖
     */
    struct ipc_message *ipc_req_msg;    /* 请求消息 buffer (Send/Call) */
    struct ipc_message *ipc_reply_msg;  /* 回复消息 buffer (Receive/Call) */
    uint32_t            notified_bits;  /* Notification 接收到的位图 */
    bool                pending_wakeup; /* 是否有挂起的唤醒信号 */
    tid_t               ipc_peer;       /* 通信对端 TID */

    /* 用户线程支持(仅用户态线程使用) */
    uint32_t ustack_top;      /* 用户态栈顶地址 */
    void    *ustack_base;     /* 用户态栈基址,用于释放 */
    void    *thread_retval;   /* pthread_exit 返回值 */
    bool     is_detached;     /* 是否为 detached 模式 */
    bool     has_been_joined; /* 是否已被 join 过,防止重复 join */
    tid_t    joiner_tid;      /* 等待此线程的 joiner TID,TID_INVALID 表示无 */

    /* 引用计数(用于 CAP) */
    uint32_t refcount;

    /* CPU 时间统计 */
    uint64_t cpu_ticks; /* 累计运行的 tick 数 */
};

/* CPU 位图操作 */
#define CPUS_ALL              0xFFFFFFFF /* 任意核 */
#define CPUS_SET(mask, cpu)   ((mask) | (1U << (cpu)))
#define CPUS_CLEAR(mask, cpu) ((mask) & ~(1U << (cpu)))
#define CPUS_TEST(mask, cpu)  ((mask) & (1U << (cpu)))
#define CPUS_ONLY(cpu)        (1U << (cpu)) /* 仅绑定单核 */

/*
 * 调度策略接口
 * 机制与策略分离:
 *   机制 (scheduler):何时调度,如何切换上下文
 *   策略 (policy):选哪个线程,如何管理队列
 */
struct sched_policy {
    const char *name;

    /* 初始化策略 */
    void (*init)(void);

    /* 线程就绪,加入运行队列 */
    void (*enqueue)(struct thread *t, cpu_id_t cpu);

    /* 线程移出运行队列 */
    void (*dequeue)(struct thread *t);

    /* 选择下一个要运行的线程(当前 CPU) */
    struct thread *(*pick_next)(void);

    /* 时钟中断处理,返回是否需要重新调度 */
    bool (*tick)(struct thread *current);

    /* 选择最适合的 CPU(负载均衡) */
    cpu_id_t (*select_cpu)(struct thread *t);
};

/*
 * Per-CPU 运行队列
 **/

struct runqueue {
    struct thread *head;       /* 就绪队列头 */
    struct thread *tail;       /* 就绪队列尾 */
    struct thread *current;    /* 当前运行线程 */
    uint32_t       nr_running; /* 运行队列长度(负载) */
};

/**
 * 获取当前 CPU 的运行队列
 */
struct runqueue *sched_get_runqueue(cpu_id_t cpu);

/**
 * 设置调度策略
 */
void sched_set_policy(struct sched_policy *policy);

/**
 * 获取当前调度策略
 */
struct sched_policy *sched_get_policy(void);

/**
 * 执行调度(切换到下一个线程)
 */
void schedule(void);

/**
 * 获取当前线程(返回 struct thread *)
 * 公共 API: thread_current() 返回 opaque thread_t
 */
struct thread *sched_current(void);

/**
 * 主动让出 CPU
 */
void sched_yield(void);

/**
 * 将线程加入阻塞链表
 */
void sched_blocked_list_add(struct thread *t);

/**
 * 带超时的阻塞
 *
 * @param wait_chan  等待通道
 * @param timeout_ms 超时时间(毫秒),0 表示无限等待
 * @return true 正常唤醒,false 超时唤醒
 */
bool sched_block_timeout(void *wait_chan, uint32_t timeout_ms);

/**
 * 从阻塞链表移除线程
 */
void sched_blocked_list_remove(struct thread *t);

/**
 * 获取阻塞链表头指针(用于遍历)
 */
struct thread **sched_get_blocked_list(void);

/**
 * 唤醒指定线程 (特定等待队列使用)
 * 将线程从阻塞链表移除并加入运行队列
 */
void sched_wakeup_thread(struct thread *t);

/**
 * 查找阻塞的线程(用于 IPC reply)
 */
struct thread *sched_lookup_blocked(tid_t tid);

/**
 * 强制终止线程
 * 从运行队列和阻塞链表移除,设置状态为 EXITED,加入僵尸链表
 */
void thread_force_exit(struct thread *t);

/**
 * 迁移线程到目标 CPU
 *
 * @param t          要迁移的线程
 * @param target_cpu 目标 CPU ID
 * @return 0 成功,-EINVAL 参数无效,-EPERM 亲和性不允许,-EBUSY 线程正在运行
 */
int sched_migrate(struct thread *t, cpu_id_t target_cpu);

/**
 * 增加线程引用计数
 */
void thread_ref(struct thread *t);

/**
 * 减少线程引用计数
 */
void thread_unref(struct thread *t);

thread_t       thread_create_with_owner(const char *name, void (*entry)(void *), void *arg,
                                        struct process *owner);
struct thread *thread_find_by_tid(tid_t tid);

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

/**
 * 将线程添加到当前 CPU 的僵尸链表
 * 用于强制退出的线程在系统调用返回时自行清理
 */
void thread_add_to_zombie_list(struct thread *t);

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
