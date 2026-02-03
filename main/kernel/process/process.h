/**
 * @file process.h
 * @brief 进程完整定义
 *
 * 进程是资源容器,包含地址空间,能力表,线程列表等.
 * 公共 API 见 <xnix/process.h>
 */

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <xnix/abi/process.h>
#include <xnix/capability.h>
#include <xnix/process.h>
#include <xnix/sync.h>
#include <xnix/types.h>

struct cap_table;  /* 前向声明 */
struct thread;     /* 前向声明 */
struct page_table; /* 前向声明 */

/**
 * 同步对象表
 *
 * 用于管理用户态线程的同步原语(主要是互斥锁).
 * 内核为每个进程维护此表,用户态通过 handle(索引)访问同步对象.
 */
struct sync_table {
    mutex_t   *mutexes[32];  /* 互斥锁数组,最多 32 个 */
    uint32_t   mutex_bitmap; /* 位图标记已分配的槽位 */
    spinlock_t lock;         /* 保护表操作 */
};

/**
 * 当前工作目录最大长度
 */
#define PROCESS_CWD_MAX 256

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

    /* 同步对象表 */
    struct sync_table *sync_table; /* 用户态线程的互斥锁等同步原语 */

    /* 用户堆 */
    uint32_t heap_start;   /* 堆起始地址(ELF 数据段之后,页对齐) */
    uint32_t heap_current; /* 当前堆顶(brk 指针) */
    uint32_t heap_max;     /* 堆上限(栈底之前) */

    /* 父子关系 */
    struct process *parent;
    struct process *children;     /* 子进程链表 */
    struct process *next_sibling; /* 兄弟进程链表 */

    /* 子进程退出等待 */
    void *wait_chan; /* 等待通道,用于 waitpid 阻塞 */

    /* 信号 */
    uint32_t pending_signals; /* 待处理信号位图 */

    /* 进程链表 */
    struct process *next; /* 全局进程链表 */

    uint32_t refcount; /* 引用计数 */

    /* 资源统计 */
    uint32_t page_count;  /* 已分配的页数(用户空间) */
    uint32_t stack_pages; /* 栈页数 */
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

/**
 * 加载用户程序到进程
 * @param proc 目标进程
 * @param path 程序路径 (目前未使用)
 * @return 0 成功, <0 失败
 */
int process_load_user(struct process *proc, const char *path);

/**
 * 用户线程入口 (用于 thread_create)
 */
void user_thread_entry(void *arg);

/**
 * 创建并启动用户进程
 * @param elf_data ELF 文件数据指针 (NULL 表示使用内置加载器)
 * @param elf_size ELF 文件大小
 */
pid_t process_spawn_init(void *elf_data, uint32_t elf_size);

/**
 * 加载 ELF 文件到进程
 */
int process_load_elf(struct process *proc, void *elf_data, uint32_t elf_size, uint32_t *out_entry);

pid_t process_spawn_module(const char *name, void *elf_data, uint32_t elf_size);

struct spawn_inherit_cap {
    cap_handle_t src;
    cap_rights_t rights;
    cap_handle_t expected_dst;
};

pid_t process_spawn_module_ex(const char *name, void *elf_data, uint32_t elf_size,
                              const struct spawn_inherit_cap *inherit_caps, uint32_t inherit_count);

/**
 * 创建进程(带 capability 继承和 argv)
 */
pid_t process_spawn_module_ex_with_args(const char *name, void *elf_data, uint32_t elf_size,
                                        const struct spawn_inherit_cap *inherit_caps,
                                        uint32_t inherit_count, int argc,
                                        char argv[][ABI_EXEC_MAX_ARG_LEN]);

/**
 * 从 ELF 创建进程(带 argv)
 * @param name     进程名
 * @param elf_data ELF 数据
 * @param elf_size ELF 大小
 * @param argc     参数数量
 * @param argv     参数数组
 */
pid_t process_spawn_elf_with_args(const char *name, void *elf_data, uint32_t elf_size, int argc,
                                  char argv[][ABI_EXEC_MAX_ARG_LEN]);

/**
 * 从 ELF 创建进程(带 capability 继承和 argv)
 */
pid_t process_spawn_elf_ex_with_args(const char *name, void *elf_data, uint32_t elf_size,
                                     const struct spawn_inherit_cap *inherit_caps,
                                     uint32_t inherit_count, int argc,
                                     char argv[][ABI_EXEC_MAX_ARG_LEN]);

/**
 * 终止当前进程
 * 用于处理用户态异常,会终止进程的所有线程
 * @param signal 导致终止的信号号(用于日志和 exit_code)
 */
void process_terminate_current(int signal);

/**
 * 进程退出处理
 * 设置退出码,通知父进程,处理子进程托管
 * @param proc 退出的进程
 * @param exit_code 退出码
 */
void process_exit(struct process *proc, int exit_code);

/**
 * 等待子进程退出
 * @param pid 目标子进程 PID,-1 表示任意子进程
 * @param status 输出退出状态
 * @param options WNOHANG 等选项
 * @return 退出的子进程 PID,0 表示无子进程退出(WNOHANG),<0 错误
 */
pid_t process_waitpid(pid_t pid, int *status, int options);

/* waitpid options */
#define WNOHANG 1 /* 非阻塞 */

/**
 * 向进程发送信号
 * @param pid 目标进程 PID
 * @param sig 信号编号
 * @return 0 成功,<0 错误
 */
int process_kill(pid_t pid, int sig);

/**
 * 检查并处理当前进程的待处理信号
 * 在返回用户态前调用
 */
void process_check_signals(void);

/* 内部变量(跨文件共享) */
extern struct process *process_list;
extern spinlock_t      process_list_lock;
extern struct process  kernel_process;

/* 内部函数 */
void free_pid(pid_t pid);

#endif
