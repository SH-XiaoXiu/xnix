/**
 * @file sched.h
 * @brief 简单调度器（验证用）
 * @author XiaoXiu
 */

#ifndef SCHED_H
#define SCHED_H

#include <arch/types.h>

#define TASK_STACK_SIZE 4096
#define MAX_TASKS       2

/**
 * @brief 任务上下文（callee-saved 寄存器）
 */
struct task_context {
    uint32_t esp;
    uint32_t ebp;
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    uint32_t eip;
};

/**
 * @brief 任务控制块
 */
struct task {
    struct task_context ctx;
    uint8_t             stack[TASK_STACK_SIZE];
    uint8_t             id;
};

/**
 * @brief 初始化调度器
 */
void sched_init(void);

/**
 * @brief 创建任务
 * @param entry 任务入口函数
 * @return 任务 ID，失败返回 -1
 */
int sched_create(void (*entry)(void));

/**
 * @brief 定时器 tick 处理（由定时器中断调用）
 */
void sched_tick(void);

/**
 * @brief 上下文切换（汇编实现）
 */
extern void context_switch(struct task_context *old, struct task_context *new);

#endif
