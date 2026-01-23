/**
 * @file thread.c
 * @brief 线程操作实现
 */

#include "sched.h"
#include <xnix/thread.h>
#include <xnix/sched.h>
#include <xnix/stdio.h>
#include <arch/cpu.h>

struct thread *thread_create(const char *name, void (*entry)(void *), void *arg) {
    return sched_spawn(name, entry, arg);
}

void thread_exit(int code) {
    struct thread *current = sched_current();
    if (current) {
        current->state     = THREAD_EXITED;
        current->exit_code = code;
        kprintf("Thread %d '%s' exited with code %d\n", current->tid, current->name, code);
        sched_destroy_current();
    }
    schedule();
    /* 不应该返回 */
    while (1) {
        cpu_halt();
    }
}

void thread_yield(void) {
    sched_yield();
}

struct thread *thread_current(void) {
    return sched_current();
}
