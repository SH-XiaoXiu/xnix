/**
 * @file main.c
 * @brief 动态显示进程信息
 */

#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>

/* ANSI 转义序列 */
#define CLEAR_SCREEN "\x1b[2J"
#define CURSOR_HOME  "\x1b[H"
#define HIDE_CURSOR  "\x1b[?25l"
#define SHOW_CURSOR  "\x1b[?25h"

/* 采样间隔(ms) */
#define SAMPLE_INTERVAL 1000

/* 最大进程数 */
#define MAX_PROCS 64

/* 上一次采样数据 */
static uint64_t prev_ticks[MAX_PROCS];
static int32_t  prev_pids[MAX_PROCS];
static int      prev_count       = 0;
static uint64_t prev_total_ticks = 0;
static uint64_t prev_idle_ticks  = 0;

static const char *state_char(uint8_t state) {
    switch (state) {
    case 0:
        return "R"; /* RUNNING */
    case 1:
        return "Z"; /* ZOMBIE */
    default:
        return "?";
    }
}

/* 查找上一次采样中该 PID 的 ticks */
static uint64_t find_prev_ticks(int32_t pid) {
    for (int i = 0; i < prev_count; i++) {
        if (prev_pids[i] == pid) {
            return prev_ticks[i];
        }
    }
    return 0;
}

/* 保存当前采样数据 */
static void save_sample(struct proc_info *procs, int count, struct sys_info *sys) {
    prev_count = count > MAX_PROCS ? MAX_PROCS : count;
    for (int i = 0; i < prev_count; i++) {
        prev_pids[i]  = procs[i].pid;
        prev_ticks[i] = procs[i].cpu_ticks;
    }
    prev_total_ticks = sys->total_ticks;
    prev_idle_ticks  = sys->idle_ticks;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    struct proc_info     procs[PROCLIST_MAX];
    struct sys_info      sys;
    struct proclist_args args;

    args.buf       = procs;
    args.buf_count = PROCLIST_MAX;
    args.sys_info  = &sys;

    /* 初始化 prev 数组 */
    memset(prev_pids, 0, sizeof(prev_pids));
    memset(prev_ticks, 0, sizeof(prev_ticks));

    printf(HIDE_CURSOR);

    /* 先采样一次获取基准值 */
    args.start_index = 0;
    int count        = sys_proclist(&args);
    if (count > 0) {
        save_sample(procs, count, &sys);
    }
    sys_sleep(SAMPLE_INTERVAL);

    while (1) {
        args.start_index = 0;

        count = sys_proclist(&args);
        if (count < 0) {
            printf(SHOW_CURSOR);
            printf("top: failed to get process list: %d\n", count);
            return 1;
        }

        /* 清屏并移动光标到左上角 */
        printf(CLEAR_SCREEN CURSOR_HOME);

        /* 计算总 tick 增量和 idle 增量 */
        uint32_t total_delta = (uint32_t)(sys.total_ticks - prev_total_ticks);
        uint32_t idle_delta  = (uint32_t)(sys.idle_ticks - prev_idle_ticks);

        /* 计算 CPU 使用率 */
        uint32_t cpu_usage = 0;
        uint32_t idle_pct  = 0;
        if (total_delta > 0) {
            idle_pct  = (idle_delta * 100) / total_delta;
            cpu_usage = 100 - idle_pct;
        }

        /* 计算总内存 */
        uint32_t total_heap  = 0;
        uint32_t total_stack = 0;
        for (int i = 0; i < count; i++) {
            total_heap += procs[i].heap_kb;
            total_stack += procs[i].stack_kb;
        }

        /* 标题 */
        printf("Xnix Task Manager\n");
        printf("CPUs: %u  |  CPU Usage: %u%%  |  Idle: %u%%\n", sys.cpu_count, cpu_usage, idle_pct);
        printf("Processes: %d  |  Memory: Heap %uK + Stack %uK = %uK\n\n", count, total_heap,
               total_stack, total_heap + total_stack);

        /* 表头 */
        printf("  PID   PPID  S  THR   %%CPU   HEAP  STACK  CPU_TIME  NAME\n");
        printf("-----  -----  -  ---  -----  -----  -----  --------  ----------------\n");

        /* 输出进程列表 */
        for (int i = 0; i < count; i++) {
            uint64_t prev  = find_prev_ticks(procs[i].pid);
            uint32_t delta = 0;
            if (procs[i].cpu_ticks >= prev) {
                delta = (uint32_t)(procs[i].cpu_ticks - prev);
            }

            /* 计算 CPU 百分比(相对于总 tick 增量) */
            uint32_t cpu_pct = 0;
            if (total_delta > 0) {
                cpu_pct = (delta * 100) / total_delta;
            }

            /* 将累计 ticks 转换为秒 */
            uint32_t ticks = (uint32_t)procs[i].cpu_ticks;
            uint32_t secs  = ticks / 100;
            uint32_t ms    = (ticks % 100) * 10;

            printf("%5d  %5d  %s  %3u  %4u%%  %4uK  %4uK  %4u.%02us  %s\n", procs[i].pid,
                   procs[i].ppid, state_char(procs[i].state), procs[i].thread_count, cpu_pct,
                   procs[i].heap_kb, procs[i].stack_kb, secs, ms / 10, procs[i].name);
        }

        printf("\nPress Ctrl+C to exit\n");

        /* 保存当前采样 */
        save_sample(procs, count, &sys);

        /* 等待 */
        sys_sleep(SAMPLE_INTERVAL);
    }

    printf(SHOW_CURSOR);
    return 0;
}
