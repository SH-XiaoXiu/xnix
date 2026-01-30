/**
 * @file init.c
 * @brief 用户态 init 进程
 *
 * init 是第一个用户进程,负责启动系统服务.
 *
 * 内核传递的 cap:
 *   handle 0: serial_ep (串口 endpoint)
 *   handle 1: io_cap (I/O 端口 capability)
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/syscall.h>

/* 自动生成的模块索引(按字母序排列驱动) */
#include <module_index.h>
/* 自动生成的 demo 模块列表 */
#include <demo_modules.h>

/* init 继承的 capability handles */
#define CAP_SERIAL_EP 0
#define CAP_IOPORT    1

static void start_seriald(void) {
    printf("[init] Starting seriald...\n");

    struct spawn_args args = {
        .name         = "seriald",
        .module_index = MODULE_SERIALD,
        .cap_count    = 2,
        .caps =
            {
                /* 传递 serial_ep 给 seriald (handle 0) */
                {.src = CAP_SERIAL_EP, .rights = CAP_READ | CAP_WRITE, .dst_hint = 0},
                /* 传递 io_cap 给 seriald (handle 1) */
                {.src = CAP_IOPORT, .rights = CAP_READ | CAP_WRITE, .dst_hint = 1},
            },
    };

    int pid = sys_spawn(&args);
    if (pid < 0) {
        printf("[init] Failed to start seriald: %d\n", pid);
    } else {
        printf("[init] seriald started (pid=%d)\n", pid);
    }
}

static void start_kbd(void) {
    printf("[init] Starting kbd...\n");

    struct spawn_args args = {
        .name         = "kbd",
        .module_index = MODULE_KBD,
        .cap_count    = 0,
    };

    int pid = sys_spawn(&args);
    if (pid < 0) {
        printf("[init] Failed to start kbd: %d\n", pid);
    } else {
        printf("[init] kbd started (pid=%d)\n", pid);
    }
}

static void start_shell(void) {
    printf("[init] Starting shell...\n");

    struct spawn_args args = {
        .name         = "shell",
        .module_index = MODULE_SHELL,
        .cap_count    = 0,
    };

    int pid = sys_spawn(&args);
    if (pid < 0) {
        printf("[init] Failed to start shell: %d\n", pid);
    } else {
        printf("[init] shell started (pid=%d)\n", pid);
    }
}

static void start_demos(void) {
#if DEMO_COUNT > 0
    printf("[init] Starting %d demo(s)...\n", DEMO_COUNT);

    for (int i = 0; i < DEMO_COUNT; i++) {
        const char *name         = demo_names[i];
        uint32_t    module_index = MODULE_DEMO_BASE + i;

        printf("[init] Starting demo '%s' (module %u)...\n", name, module_index);

        struct spawn_args args = {0};
        /* 复制名称 */
        for (int j = 0; name[j] && j < 15; j++) {
            args.name[j] = name[j];
        }
        args.module_index = module_index;
        args.cap_count    = 0;

        int pid = sys_spawn(&args);
        if (pid < 0) {
            printf("[init] Failed to start '%s': %d\n", name, pid);
        } else {
            printf("[init] '%s' started (pid=%d)\n", name, pid);
        }
    }
#else
    printf("[init] No demos configured\n");
#endif
}

static void reap_children(void) {
    int status;
    int pid;

    /* 非阻塞收割所有已退出的子进程 */
    while ((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[init] Reaped child process %d (status=%d)\n", pid, status);
    }
}

int main(void) {
    printf("[init] init process started (PID %d)\n", sys_getpid());

    /* 启动 seriald 服务 */
    start_seriald();

    /* 等待 seriald 初始化 */
    sleep(1);

    /* 启动 kbd 服务 */
    start_kbd();

    /* 启动 shell */
    start_shell();

    printf("[init] System ready\n");

    /* 收割僵尸子进程 */
    while (1) {
        reap_children();
        msleep(100);
    }
    return 0;
}
