/**
 * @file lifecycle.c
 * @brief 进程生命周期(退出,等待,信号)
 */

#include "process_internal.h"

#include <arch/cpu.h>

#include <xnix/debug.h>
#include <xnix/errno.h>
#include <xnix/process_def.h>
#include <xnix/signal.h>
#include <xnix/stdio.h>
#include <xnix/thread.h>
#include <xnix/thread_def.h>

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

/**
 * 将子进程托管给 init 进程
 */
static void process_reparent_children(struct process *proc) {
    if (!proc->children) {
        return;
    }

    /* 找到 init 进程(PID 1) */
    struct process *init = process_find_by_pid(XNIX_PID_INIT);
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

void process_terminate_current(int signal) {
    struct process *proc    = process_get_current();
    struct thread  *current = sched_current();

    /* 不能终止内核进程 */
    if (!proc || proc->pid == 0) {
        panic("Attempt to terminate kernel process!");
    }

    /* init 进程终止是致命的 */
    if (!proc || proc->pid == XNIX_PID_INIT) {
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

void process_exit(struct process *proc, int exit_code) {
    if (!proc || proc->pid == 0) {
        return;
    }

    kprintf("[exit] Process '%s' (pid=%d) exiting with code %d\n", proc->name, proc->pid,
            exit_code);

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
