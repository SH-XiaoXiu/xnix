/**
 * @file main.c
 * @brief 列出进程信息
 */

#include <errno.h>
#include <stdio.h>
#include <xnix/syscall.h>

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

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    struct proc_info     procs[PROCLIST_MAX];
    struct sys_info      sys;
    struct proclist_args args;

    args.buf         = procs;
    args.buf_count   = PROCLIST_MAX;
    args.start_index = 0;
    args.sys_info    = &sys;

    int count = sys_proclist(&args);
    if (count < 0) {
        printf("ps: failed to get process list: %s\n", strerror(-count));
        return 1;
    }

    /* 系统信息 */
    printf("CPUs: %u\n\n", sys.cpu_count);

    /* 表头 */
    printf("  PID   PPID  S  THR   HEAP  STACK  CPU_TIME  NAME\n");
    printf("-----  -----  -  ---  -----  -----  --------  ----------------\n");

    /* 输出进程列表 */
    for (int i = 0; i < count; i++) {
        /* 将 ticks 转换为秒(假设 100 Hz) */
        uint32_t ticks = (uint32_t)procs[i].cpu_ticks;
        uint32_t secs  = ticks / 100;
        uint32_t ms    = (ticks % 100) * 10;

        printf("%5d  %5d  %s  %3u  %4uK  %4uK  %4u.%02us  %s\n", procs[i].pid, procs[i].ppid,
               state_char(procs[i].state), procs[i].thread_count, procs[i].heap_kb,
               procs[i].stack_kb, secs, ms / 10, procs[i].name);
    }

    return 0;
}
