/**
 * @file process.c
 * @brief 进程管理实现
 */

#include <arch/cpu.h>

#include <kernel/capability/capability.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <kernel/vfs/vfs.h>
#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/signal.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/thread.h>

/* 声明 vmm_kmap/kunmap */
extern void *vmm_kmap(paddr_t paddr);
extern void  vmm_kunmap(void *vaddr);

/* 全局进程链表 */
static struct process *process_list = NULL;
static spinlock_t      process_list_lock;

/* PID Bitmap */
static uint32_t *pid_bitmap   = NULL;
static uint32_t  pid_capacity = 0;

/* 内核进程(PID 0) */
static struct process kernel_process;

void process_subsystem_init(void) {
    /* 分配 PID Bitmap */
    pid_capacity       = (CFG_INITIAL_PROCESSES + 31) & ~31;
    size_t bitmap_size = (pid_capacity / 8);
    pid_bitmap         = kzalloc(bitmap_size);
    if (!pid_bitmap) {
        panic("Failed to allocate PID bitmap");
    }

    /* 初始化内核进程 */
    kernel_process.pid = 0;
    /* 占用 PID 0 */
    pid_bitmap[0] |= 1;

    kernel_process.name          = "kernel";
    kernel_process.state         = PROCESS_RUNNING;
    kernel_process.exit_code     = 0;
    kernel_process.page_dir_phys = NULL; /* 内核进程使用当前(或内核)页表 */
    kernel_process.cap_table     = cap_table_create();
    kernel_process.threads       = NULL;
    kernel_process.thread_count  = 0;
    kernel_process.thread_lock   = mutex_create();
    kernel_process.sync_table    = kzalloc(sizeof(struct sync_table));
    kernel_process.parent        = NULL;
    kernel_process.children      = NULL;
    kernel_process.next_sibling  = NULL;
    kernel_process.next          = NULL;
    kernel_process.refcount      = 1;
    kernel_process.cwd[0]        = '/';
    kernel_process.cwd[1]        = '\0';

    if (kernel_process.sync_table) {
        spin_init(&kernel_process.sync_table->lock);
        kernel_process.sync_table->mutex_bitmap = 0;
    }

    process_list = &kernel_process;

    /* 注册 PROCESS 能力类型 */
    cap_register_type(CAP_TYPE_PROCESS, (cap_ref_fn)process_ref, (cap_unref_fn)process_unref);

    pr_info("Process subsystem initialized (kernel PID 0)");
}

static void free_pid(pid_t pid) {
    if (pid == 0 || pid >= (int32_t)pid_capacity) {
        return;
    }

    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);
    pid_bitmap[pid / 32] &= ~(1UL << (pid % 32));
    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);
}

pid_t process_alloc_pid(void) {
    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);

    for (uint32_t i = 0; i < (pid_capacity + 31) / 32; i++) {
        if (pid_bitmap[i] == 0xFFFFFFFF) {
            continue;
        }

        for (int j = 0; j < 32; j++) {
            uint32_t pid = i * 32 + j;
            if (pid >= pid_capacity) {
                break;
            }

            if (!((pid_bitmap[i] >> j) & 1)) {
                pid_bitmap[i] |= (1UL << j);
                spin_unlock(&process_list_lock);
                cpu_irq_restore(flags);
                return pid;
            }
        }
    }

    /* 扩容 */
    uint32_t  new_capacity = pid_capacity * 2;
    uint32_t *new_bitmap   = kzalloc((new_capacity + 31) / 8);
    if (!new_bitmap) {
        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);
        return PID_INVALID;
    }

    memcpy(new_bitmap, pid_bitmap, (pid_capacity + 31) / 8);
    kfree(pid_bitmap);
    pid_bitmap   = new_bitmap;
    pid_capacity = new_capacity;

    /* 返回新扩容部分的第一个 ID */
    pid_t pid = pid_capacity / 2;
    pid_bitmap[pid / 32] |= (1UL << (pid % 32));

    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);
    return pid;
}

struct process *process_find_by_pid(pid_t pid) {
    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);

    struct process *proc = process_list;
    while (proc) {
        if (proc->pid == pid) {
            process_ref(proc);
            spin_unlock(&process_list_lock);
            cpu_irq_restore(flags);
            return proc;
        }
        proc = proc->next;
    }

    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);
    return NULL;
}

void process_ref(struct process *proc) {
    if (!proc) {
        return;
    }

    uint32_t flags = cpu_irq_save();
    proc->refcount++;
    cpu_irq_restore(flags);
}

void process_unref(struct process *proc) {
    if (!proc) {
        return;
    }

    uint32_t flags = cpu_irq_save();
    proc->refcount--;
    if (proc->refcount == 0) {
        cpu_irq_restore(flags);

        /* 销毁进程 */
        if (proc->page_dir_phys) {
            const struct mm_operations *mm = mm_get_ops();
            if (mm && mm->destroy_as) {
                mm->destroy_as(proc->page_dir_phys);
            }
            proc->page_dir_phys = NULL;
        }
        if (proc->cap_table) {
            cap_table_destroy(proc->cap_table);
        }
        if (proc->thread_lock) {
            mutex_destroy(proc->thread_lock);
        }
        if (proc->sync_table) {
            for (uint32_t i = 0; i < 32; i++) {
                if (proc->sync_table->mutexes[i]) {
                    mutex_destroy(proc->sync_table->mutexes[i]);
                    proc->sync_table->mutexes[i] = NULL;
                }
            }
            kfree(proc->sync_table);
            proc->sync_table = NULL;
        }
        if (proc->fd_table) {
            fd_table_destroy(proc->fd_table);
            proc->fd_table = NULL;
        }

        /* 从进程链表移除 */
        flags = cpu_irq_save();
        spin_lock(&process_list_lock);

        struct process **pp = &process_list;
        while (*pp) {
            if (*pp == proc) {
                *pp = proc->next;
                break;
            }
            pp = &(*pp)->next;
        }

        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);

        /* 释放进程名 */
        if (proc->name) {
            kfree((void *)proc->name);
        }

        free_pid(proc->pid);
        kfree(proc);
        return;
    }
    cpu_irq_restore(flags);
}

process_t process_create(const char *name) {
    struct process *proc = kzalloc(sizeof(struct process));
    if (!proc) {
        return NULL;
    }

    proc->pid = process_alloc_pid();
    if (proc->pid == PID_INVALID) {
        kfree(proc);
        return NULL;
    }

    /* 复制进程名(防止外部字符串被释放或覆盖) */
    const char *src       = name ? name : "?";
    size_t      len       = strlen(src);
    char       *name_copy = kmalloc(len + 1);
    if (name_copy) {
        memcpy(name_copy, src, len + 1);
    }
    proc->name      = name_copy; /* NULL 表示分配失败 */
    proc->state     = PROCESS_RUNNING;
    proc->exit_code = 0;

    /* 创建地址空间 */
    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->create_as) {
        panic("MM ops not initialized");
    }
    proc->page_dir_phys = mm->create_as();
    if (!proc->page_dir_phys) {
        free_pid(proc->pid);
        kfree(proc);
        return NULL;
    }
    proc->cap_table    = cap_table_create();
    proc->threads      = NULL;
    proc->thread_count = 0;
    proc->thread_lock  = mutex_create();
    proc->sync_table   = kzalloc(sizeof(struct sync_table));
    proc->parent       = NULL;
    proc->children     = NULL;
    proc->next_sibling = NULL;
    proc->refcount     = 1;
    proc->cwd[0]       = '/';
    proc->cwd[1]       = '\0';

    if (proc->sync_table) {
        spin_init(&proc->sync_table->lock);
        proc->sync_table->mutex_bitmap = 0;
    }

    /* 创建 fd 表 */
    proc->fd_table = fd_table_create();

    if (!proc->cap_table || !proc->thread_lock || !proc->sync_table || !proc->fd_table) {
        if (proc->page_dir_phys) {
            if (mm->destroy_as) {
                mm->destroy_as(proc->page_dir_phys);
            }
        }
        if (proc->cap_table) {
            cap_table_destroy(proc->cap_table);
        }
        if (proc->thread_lock) {
            mutex_destroy(proc->thread_lock);
        }
        if (proc->sync_table) {
            kfree(proc->sync_table);
        }
        if (proc->fd_table) {
            fd_table_destroy(proc->fd_table);
        }
        kfree(proc);
        return NULL;
    }

    /* 加入进程链表 */
    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);

    proc->next   = process_list;
    process_list = proc;

    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);

    return proc;
}

void process_destroy(process_t proc) {
    if (!proc) {
        return;
    }

    process_unref(proc);
}

/* 前向声明 */
static void process_reparent_children(struct process *proc);

/**
 * 终止进程的所有其他线程
 */
static void process_terminate_threads(struct process *proc, struct thread *except) {
    mutex_lock(proc->thread_lock);

    struct thread *t = proc->threads;
    while (t) {
        struct thread *next = t->proc_next;
        if (t != except) {
            thread_force_exit(t);
        }
        t = next;
    }

    mutex_unlock(proc->thread_lock);
}

void process_terminate_current(int signal) {
    struct process *proc    = process_get_current();
    struct thread  *current = sched_current();

    /* 不能终止内核进程 */
    if (!proc || proc->pid == 0) {
        panic("Attempt to terminate kernel process!");
    }

    /* init 进程终止是致命的 */
    if (!proc || proc->pid == 1) {
        panic("Init process terminated by signal %d!", signal);
    }

    pr_info("Process %d '%s' terminated (signal %d)", proc->pid, proc->name ? proc->name : "?",
            signal);

    proc->state     = PROCESS_ZOMBIE;
    proc->exit_code = -signal;

    /* 将子进程托管给 init */
    process_reparent_children(proc);

    /* 唤醒等待的父进程 */
    struct process *parent = proc->parent;
    if (parent && parent->wait_chan && parent->threads) {
        sched_wakeup_thread(parent->threads);
    }

    /* 终止进程的所有其他线程 */
    process_terminate_threads(proc, current);

    /* 当前线程退出(会触发调度) */
    thread_exit(-signal);
    __builtin_unreachable();
}

void process_add_thread(struct process *proc, struct thread *t) {
    if (!proc || !t) {
        return;
    }

    mutex_lock(proc->thread_lock);

    t->proc_next  = proc->threads;
    proc->threads = t;
    t->owner      = proc;
    proc->thread_count++;

    mutex_unlock(proc->thread_lock);
}

void process_remove_thread(struct process *proc, struct thread *t) {
    if (!proc || !t) {
        return;
    }

    mutex_lock(proc->thread_lock);

    struct thread **pp = &proc->threads;
    while (*pp) {
        if (*pp == t) {
            *pp          = t->proc_next;
            t->proc_next = NULL;
            t->owner     = NULL;
            proc->thread_count--;
            break;
        }
        pp = &(*pp)->proc_next;
    }

    mutex_unlock(proc->thread_lock);
}

struct process *process_get_current(void) {
    struct thread *t = sched_current();
    if (!t) {
        return &kernel_process;
    }

    if (!t->owner) {
        return &kernel_process;
    }

    return t->owner;
}

process_t process_current(void) {
    return process_get_current();
}

pid_t process_get_pid(process_t proc) {
    return proc ? proc->pid : PID_INVALID;
}

const char *process_get_name(process_t proc) {
    return proc ? proc->name : NULL;
}

process_state_t process_get_state(process_t proc) {
    return proc ? proc->state : PROCESS_ZOMBIE;
}

void process_init(void) {
    process_subsystem_init();
}

/* 声明架构相关的用户态跳转函数 */
extern void enter_user_mode(uint32_t eip, uint32_t esp);

/* 声明内部函数 */
extern thread_t thread_create_with_owner(const char *name, void (*entry)(void *), void *arg,
                                         struct process *owner);

/* 用户栈配置 */
#define USER_STACK_TOP 0xBFFFF000

/*
 * 用户线程入口
 * 当线程第一次被调度时,会从这里开始执行
 *
 * 需要在用户栈上设置 argc=0, argv=NULL 以兼容新的 CRT0
 */
void user_thread_entry(void *arg) {
    /*
     * 此时已经处于内核态,并且 CR3 已经切换到了目标进程的页表
     * 需要在用户栈上设置 argc/argv,然后跳转到用户态
     *
     * 栈布局:
     *   [esp+0] = argc (0)
     *   [esp+4] = argv (NULL)
     */
    struct process *proc = process_get_current();
    if (!proc) {
        panic("No current process in user_thread_entry");
    }

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        panic("No mm_ops in user_thread_entry");
    }

    /* 栈顶在 USER_STACK_TOP,需要设置 argc/argv(共 8 字节,16 字节对齐) */
    uint32_t final_esp       = (USER_STACK_TOP - 16) & ~15;
    uint32_t esp_page_vaddr  = final_esp & ~(PAGE_SIZE - 1);
    uint32_t esp_page_offset = final_esp & (PAGE_SIZE - 1);
    paddr_t  esp_paddr       = (paddr_t)mm->query(proc->page_dir_phys, esp_page_vaddr);
    if (esp_paddr) {
        void     *mapped = vmm_kmap(esp_paddr);
        uint32_t *stack  = (uint32_t *)((uint8_t *)mapped + esp_page_offset);
        stack[0]         = 0; /* argc = 0 */
        stack[1]         = 0; /* argv = NULL */
        vmm_kunmap(mapped);
    }

    enter_user_mode((uint32_t)arg, final_esp);

    /* 永远不会返回 */
    panic("Returned from user mode!");
}

pid_t process_spawn_init(void *elf_data, uint32_t elf_size) {
    return process_spawn_module("init", elf_data, elf_size);
}

pid_t process_spawn_module(const char *name, void *elf_data, uint32_t elf_size) {
    return process_spawn_module_ex(name, elf_data, elf_size, NULL, 0);
}

pid_t process_spawn_module_ex(const char *name, void *elf_data, uint32_t elf_size,
                              const struct spawn_inherit_cap *inherit_caps,
                              uint32_t                        inherit_count) {
    struct process *proc = (struct process *)process_create(name);
    if (!proc) {
        pr_err("Failed to create process");
        return PID_INVALID;
    }

    struct process *creator = process_get_current();

    /* 设置父子关系 */
    proc->parent = creator;
    if (creator) {
        uint32_t flags = cpu_irq_save();
        spin_lock(&process_list_lock);
        proc->next_sibling = creator->children;
        creator->children  = proc;
        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);

        /* 继承父进程的 cwd */
        strncpy(proc->cwd, creator->cwd, PROCESS_CWD_MAX - 1);
        proc->cwd[PROCESS_CWD_MAX - 1] = '\0';
    }
    for (uint32_t i = 0; i < inherit_count; i++) {
        cap_handle_t dup =
            cap_duplicate_to(creator, inherit_caps[i].src, proc, inherit_caps[i].rights);
        if (dup == CAP_HANDLE_INVALID) {
            pr_err("Failed to inherit capability for %s", name ? name : "?");
            process_destroy((process_t)proc);
            return PID_INVALID;
        }
        if (inherit_caps[i].expected_dst != CAP_HANDLE_INVALID &&
            dup != inherit_caps[i].expected_dst) {
            pr_warn("Boot: inherited handle mismatch (%u -> %u)", inherit_caps[i].expected_dst,
                    dup);
        }
    }

    int      ret;
    uint32_t entry_point = 0;
    if (elf_data) {
        ret = process_load_elf(proc, elf_data, elf_size, &entry_point);
    } else {
        pr_err("No module provided");
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    if (ret < 0) {
        pr_err("Failed to load program: %d", ret);
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    thread_t t =
        thread_create_with_owner("bootstrap", user_thread_entry, (void *)entry_point, proc);
    if (!t) {
        pr_err("Failed to create process thread");
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    process_add_thread(proc, (struct thread *)t);
    pr_ok("Spawned %s process (PID %d)", name ? name : "?", proc->pid);
    return proc->pid;
}

/**
 * 将子进程托管给 init 进程
 */
static void process_reparent_children(struct process *proc) {
    if (!proc->children) {
        return;
    }

    /* 找到 init 进程(PID 1) */
    struct process *init = process_find_by_pid(1);
    if (!init) {
        /* 没有 init 进程,直接断开 */
        proc->children = NULL;
        return;
    }

    uint32_t flags = cpu_irq_save();
    spin_lock(&process_list_lock);

    /* 将所有子进程挂到 init 下 */
    struct process *child = proc->children;
    while (child) {
        struct process *next = child->next_sibling;
        child->parent        = init;
        child->next_sibling  = init->children;
        init->children       = child;

        /* 如果子进程已经是僵尸,唤醒 init */
        if (child->state == PROCESS_ZOMBIE && init->wait_chan && init->threads) {
            sched_wakeup_thread(init->threads);
        }
        child = next;
    }
    proc->children = NULL;

    spin_unlock(&process_list_lock);
    cpu_irq_restore(flags);

    process_unref(init);
}

void process_exit(struct process *proc, int exit_code) {
    if (!proc || proc->pid == 0) {
        return;
    }

    proc->state     = PROCESS_ZOMBIE;
    proc->exit_code = exit_code;

    /* 将子进程托管给 init */
    process_reparent_children(proc);

    /* 唤醒等待的父进程(仅当父进程在 waitpid 中等待时) */
    struct process *parent = proc->parent;
    if (parent && parent->wait_chan && parent->threads) {
        sched_wakeup_thread(parent->threads);
    }
}

pid_t process_waitpid(pid_t pid, int *status, int options) {
    struct process *current = process_get_current();
    if (!current) {
        return -ESRCH;
    }

    /* 提前设置 wait_chan,避免与子进程退出的竞态 */
    current->wait_chan = current;

    while (1) {
        uint32_t flags = cpu_irq_save();
        spin_lock(&process_list_lock);

        /* 查找匹配的僵尸子进程 */
        struct process *child     = current->children;
        struct process *prev      = NULL;
        struct process *found     = NULL;
        bool            has_child = false;

        while (child) {
            if (pid == -1 || child->pid == pid) {
                has_child = true;
                if (child->state == PROCESS_ZOMBIE) {
                    found = child;
                    /* 从子进程链表移除 */
                    if (prev) {
                        prev->next_sibling = child->next_sibling;
                    } else {
                        current->children = child->next_sibling;
                    }
                    break;
                }
            }
            prev  = child;
            child = child->next_sibling;
        }

        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);

        if (found) {
            pid_t ret_pid = found->pid;
            if (status) {
                *status = found->exit_code;
            }
            found->parent       = NULL;
            found->next_sibling = NULL;
            process_unref(found);
            current->wait_chan = NULL;
            return ret_pid;
        }

        if (!has_child) {
            current->wait_chan = NULL;
            return -ECHILD;
        }

        if (options & WNOHANG) {
            current->wait_chan = NULL;
            return 0;
        }

        /* 阻塞等待子进程退出 */
        sched_block(current->wait_chan);
    }
}

int process_kill(pid_t pid, int sig) {
    if (sig < 1 || sig >= NSIG) {
        return -EINVAL;
    }

    struct process *proc = process_find_by_pid(pid);
    if (!proc) {
        return -ESRCH;
    }

    /* 不能向内核进程发信号 */
    if (proc->pid == 0) {
        process_unref(proc);
        return -EPERM;
    }

    /* 设置待处理信号 */
    uint32_t flags = cpu_irq_save();
    proc->pending_signals |= sigmask(sig);
    cpu_irq_restore(flags);

    /* 唤醒进程的主线程处理信号 */
    struct thread *t = proc->threads;
    if (t) {
        sched_wakeup_thread(t);
    }

    process_unref(proc);
    return 0;
}

void process_check_signals(void) {
    struct process *proc = process_get_current();
    if (!proc || proc->pid == 0) {
        return;
    }

    uint32_t pending = proc->pending_signals;
    if (!pending) {
        return;
    }

    /* 处理致命信号 */
    if (pending & (sigmask(SIGKILL) | sigmask(SIGINT) | sigmask(SIGTERM) | sigmask(SIGSEGV))) {
        int sig = 0;
        if (pending & sigmask(SIGKILL)) {
            sig = SIGKILL;
        } else if (pending & sigmask(SIGINT)) {
            sig = SIGINT;
        } else if (pending & sigmask(SIGTERM)) {
            sig = SIGTERM;
        } else if (pending & sigmask(SIGSEGV)) {
            sig = SIGSEGV;
        }

        proc->pending_signals &= ~sigmask(sig);
        process_terminate_current(sig);
        /* 不会返回 */
    }
}

/**
 * 用户线程入口(带 argv)
 * arg 指向 argv_info 结构
 */
struct argv_info {
    uint32_t entry_point;
    uint32_t stack_top;
};

void user_thread_entry_with_args(void *arg) {
    struct argv_info *info = (struct argv_info *)arg;
    enter_user_mode(info->entry_point, info->stack_top);
    panic("Returned from user mode!");
}

pid_t process_spawn_elf_with_args(const char *name, void *elf_data, uint32_t elf_size, int argc,
                                  char argv[][ABI_EXEC_MAX_ARG_LEN]) {
    struct process *proc = (struct process *)process_create(name);
    if (!proc) {
        pr_err("Failed to create process");
        return PID_INVALID;
    }

    struct process *creator = process_get_current();

    /* 设置父子关系 */
    proc->parent = creator;
    if (creator) {
        uint32_t flags = cpu_irq_save();
        spin_lock(&process_list_lock);
        proc->next_sibling = creator->children;
        creator->children  = proc;
        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);

        /* 继承父进程的 cwd */
        strncpy(proc->cwd, creator->cwd, PROCESS_CWD_MAX - 1);
        proc->cwd[PROCESS_CWD_MAX - 1] = '\0';
    }

    int      ret;
    uint32_t entry_point = 0;
    if (elf_data) {
        ret = process_load_elf(proc, elf_data, elf_size, &entry_point);
    } else {
        pr_err("No ELF data provided");
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    if (ret < 0) {
        pr_err("Failed to load ELF: %d", ret);
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    /* 设置 argv 到用户栈 */
    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    /*
     * 用户栈布局(从高地址到低地址):
     *
     *   高地址
     *   +------------------+
     *   | argv 字符串      |  "arg0\0" "arg1\0" ...
     *   +------------------+
     *   | padding (对齐)   |
     *   +------------------+
     *   | NULL             |  argv[argc]
     *   +------------------+
     *   | argv[argc-1]     |  指向 "arg(argc-1)"
     *   | ...              |
     *   | argv[0]          |  指向 "arg0"
     *   +------------------+
     *   | argv 指针        |  指向 argv[0]
     *   +------------------+
     *   | argc             |
     *   +------------------+  <- ESP
     *   低地址
     */

    uint32_t stack_top = USER_STACK_TOP;

    /* 计算字符串总长度 */
    uint32_t strings_size = 0;
    for (int i = 0; i < argc; i++) {
        strings_size += strlen(argv[i]) + 1;
    }

    /* 在栈顶放置字符串 */
    uint32_t strings_start = stack_top - strings_size;
    strings_start &= ~3; /* 4 字节对齐 */

    /* argv 数组(包含 NULL 结尾) */
    uint32_t argv_array_size = (argc + 1) * sizeof(uint32_t);
    uint32_t argv_array_addr = strings_start - argv_array_size;
    argv_array_addr &= ~3;

    /* argc 和 argv 指针 */
    uint32_t final_esp = argv_array_addr - 8; /* argc + argv ptr */
    final_esp &= ~15;                         /* 16 字节对齐 */

    /* 写入字符串和 argv 数组 */
    uint32_t str_offset = strings_start;
    for (int i = 0; i < argc; i++) {
        uint32_t len = strlen(argv[i]) + 1;

        /* 查找字符串所在的物理页并写入 */
        uint32_t page_vaddr  = str_offset & ~(PAGE_SIZE - 1);
        uint32_t page_offset = str_offset & (PAGE_SIZE - 1);
        paddr_t  paddr       = (paddr_t)mm->query(proc->page_dir_phys, page_vaddr);
        if (!paddr) {
            pr_err("Stack page not mapped for argv strings");
            process_destroy((process_t)proc);
            return PID_INVALID;
        }

        void *mapped = vmm_kmap(paddr);
        memcpy((uint8_t *)mapped + page_offset, argv[i], len);
        vmm_kunmap(mapped);

        /* 写入 argv[i] 指针 */
        uint32_t argv_entry_addr  = argv_array_addr + i * sizeof(uint32_t);
        uint32_t argv_page_vaddr  = argv_entry_addr & ~(PAGE_SIZE - 1);
        uint32_t argv_page_offset = argv_entry_addr & (PAGE_SIZE - 1);
        paddr_t  argv_paddr       = (paddr_t)mm->query(proc->page_dir_phys, argv_page_vaddr);
        if (!argv_paddr) {
            pr_err("Stack page not mapped for argv array");
            process_destroy((process_t)proc);
            return PID_INVALID;
        }

        mapped                                              = vmm_kmap(argv_paddr);
        *(uint32_t *)((uint8_t *)mapped + argv_page_offset) = str_offset;
        vmm_kunmap(mapped);

        str_offset += len;
    }

    /* 写入 argv[argc] = NULL */
    uint32_t null_addr        = argv_array_addr + argc * sizeof(uint32_t);
    uint32_t null_page_vaddr  = null_addr & ~(PAGE_SIZE - 1);
    uint32_t null_page_offset = null_addr & (PAGE_SIZE - 1);
    paddr_t  null_paddr       = (paddr_t)mm->query(proc->page_dir_phys, null_page_vaddr);
    if (null_paddr) {
        void *mapped                                        = vmm_kmap(null_paddr);
        *(uint32_t *)((uint8_t *)mapped + null_page_offset) = 0;
        vmm_kunmap(mapped);
    }

    /* 写入 argc 和 argv 指针到 final_esp 位置 */
    uint32_t esp_page_vaddr  = final_esp & ~(PAGE_SIZE - 1);
    uint32_t esp_page_offset = final_esp & (PAGE_SIZE - 1);
    paddr_t  esp_paddr       = (paddr_t)mm->query(proc->page_dir_phys, esp_page_vaddr);
    if (esp_paddr) {
        void     *mapped = vmm_kmap(esp_paddr);
        uint32_t *stack  = (uint32_t *)((uint8_t *)mapped + esp_page_offset);
        stack[0]         = (uint32_t)argc;  /* argc */
        stack[1]         = argv_array_addr; /* argv */
        vmm_kunmap(mapped);
    }

    /* 分配 argv_info 结构(需要在内核堆中分配,线程结束后释放) */
    struct argv_info *info = kmalloc(sizeof(struct argv_info));
    if (!info) {
        process_destroy((process_t)proc);
        return PID_INVALID;
    }
    info->entry_point = entry_point;
    info->stack_top   = final_esp;

    thread_t t = thread_create_with_owner("bootstrap", user_thread_entry_with_args, info, proc);
    if (!t) {
        pr_err("Failed to create process thread");
        kfree(info);
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    process_add_thread(proc, (struct thread *)t);
    pr_debug("Spawned %s process (PID %d) with %d args", name ? name : "?", proc->pid, argc);
    return proc->pid;
}
