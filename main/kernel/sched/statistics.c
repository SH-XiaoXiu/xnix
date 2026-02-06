/**
 * @file statistics.c
 * @brief 调度器统计信息
 */

#include "sched_internal.h"

#include <xnix/thread_def.h>

/* 系统统计变量 */
static uint64_t global_ticks = 0; /* 全局 tick 计数 */
static uint64_t idle_ticks   = 0; /* idle 线程累计 ticks */

/*
 * 内部接口(供 sched.c 使用)
 */

void sched_stat_tick(void) {
    global_ticks++;
}

void sched_stat_idle_tick(void) {
    idle_ticks++;
}

/*
 * 公共 API
 */

uint64_t sched_get_global_ticks(void) {
    return global_ticks;
}

uint64_t sched_get_idle_ticks(void) {
    return idle_ticks;
}
