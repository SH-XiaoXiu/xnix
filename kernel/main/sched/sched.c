/**
 * @file sched.c
 * @brief 简单调度器实现
 * @author XiaoXiu
 */

#include "sched.h"
#include <xstd/stdio.h>

static struct task tasks[MAX_TASKS];
static int task_count = 0;
static int current_task = -1;  /* -1 表示还没开始调度 */

static void task_exit(void) {
    kprintf("Task %d exited!\n", current_task);
    while (1) {
        __asm__ volatile("hlt");
    }
}

/* 任务入口包装器：开中断后再调用真正的入口 */
static void (*task_entries[MAX_TASKS])(void);

static void task_wrapper(void) {
    __asm__ volatile("sti");  /* 开中断 */
    task_entries[current_task]();
}

void sched_init(void) {
    task_count = 0;
    current_task = -1;  /* 还没开始调度 */
    kprintf("Scheduler initialized\n");
}

int sched_create(void (*entry)(void)) {
    if (task_count >= MAX_TASKS) {
        return -1;
    }

    struct task *t = &tasks[task_count];
    t->id = task_count;

    /* 保存真正的入口地址 */
    task_entries[task_count] = entry;

    /* 设置栈顶 */
    uint32_t *stack_top = (uint32_t *)(t->stack + TASK_STACK_SIZE);

    /* 压入返回地址（任务退出时跳转） */
    *(--stack_top) = (uint32_t)task_exit;

    /* 压入 wrapper 地址（context_switch 的 ret 会跳转到这里，wrapper 会开中断） */
    *(--stack_top) = (uint32_t)task_wrapper;

    /* 初始化上下文 */
    t->ctx.esp = (uint32_t)stack_top;
    t->ctx.ebp = 0;
    t->ctx.ebx = 0;
    t->ctx.esi = 0;
    t->ctx.edi = 0;
    t->ctx.eip = (uint32_t)entry;

    kprintf("Task %d created, entry=0x%x\n", task_count, (uint32_t)entry);
    return task_count++;
}

/* 第一次启动任务（不保存旧上下文） */
extern void context_switch_first(struct task_context *new);

void sched_tick(void) {
    if (task_count == 0) {
        return;
    }

    /* 第一次调度：直接跳转到任务 0 */
    if (current_task < 0) {
        current_task = 0;
        context_switch_first(&tasks[0].ctx);
        return;
    }

    if (task_count < 2) {
        return;
    }

    int prev = current_task;
    current_task = (current_task + 1) % task_count;

    if (prev != current_task) {
        context_switch(&tasks[prev].ctx, &tasks[current_task].ctx);
    }
}
