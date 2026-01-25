/**
 * @file process.h
 * @brief 进程完整定义
 *
 * 进程是资源容器,包含地址空间,能力表,线程列表等.
 * 公共 API 见 <xnix/process.h>
 */

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <xnix/process.h>
#include <xnix/sync.h>
#include <xnix/types.h>

struct cap_table;  /* 前向声明 */
struct thread;     /* 前向声明 */
struct page_table; /* 前向声明 */

/**
 * 进程控制块 (PCB)
 */
struct process {
    pid_t       pid;
    const char *name;

    process_state_t state;
    int             exit_code;

    /* 地址空间 (页目录物理地址) */
    void *page_dir_phys;

    /* 能力表 */
    struct cap_table *cap_table;

    /* 线程列表 */
    struct thread *threads;      /* 属于此进程的线程链表 */
    uint32_t       thread_count; /* 线程数 */
    mutex_t       *thread_lock;  /* 保护线程列表 */

    /* 父子关系 */
    struct process *parent;
    struct process *children;     /* 子进程链表 */
    struct process *next_sibling; /* 兄弟进程链表 */

    /* 进程链表 */
    struct process *next; /* 全局进程链表 */

    uint32_t refcount; /* 引用计数 */
};

/**
 * 初始化进程管理
 */
void process_subsystem_init(void);

/**
 * 分配 PID
 */
pid_t process_alloc_pid(void);

/**
 * 根据 PID 查找进程
 */
struct process *process_find_by_pid(pid_t pid);

/**
 * 增加引用计数
 */
void process_ref(struct process *proc);

/**
 * 减少引用计数,为 0 时销毁
 */
void process_unref(struct process *proc);

/**
 * 将线程添加到进程
 */
void process_add_thread(struct process *proc, struct thread *t);

/**
 * 从进程移除线程
 */
void process_remove_thread(struct process *proc, struct thread *t);

/**
 * 获取当前进程(基于 thread_current())
 */
struct process *process_get_current(void);

#endif
