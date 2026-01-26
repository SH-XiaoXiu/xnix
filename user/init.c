/**
 * @file init.c
 * @brief 用户态 init 进程
 *
 * init 是第一个用户进程，负责启动系统服务。
 *
 * 内核传递的 cap:
 *   handle 0: serial_ep (串口 endpoint)
 *   handle 1: io_cap (I/O 端口 capability)
 */

#include <stdio.h>
#include <unistd.h>
#include <xnix/syscall.h>

/* 模块索引约定: 0=init, 1=seriald */
#define MODULE_SERIALD 1

/* init 继承的 capability handles */
#define CAP_SERIAL_EP 0
#define CAP_IOPORT    1

static void start_seriald(void) {
    printf("[init] Starting seriald...\n");

    struct spawn_args args = {
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

int main(void) {
    printf("[init] init process started\n");

    /* 启动 seriald 服务 */
    start_seriald();

    /* 等待 seriald 初始化 */
    sleep(1);

    printf("[init] System ready\n");

    /* 主循环 */
    int i = 0;
    while (1) {
        sleep(5);
        i++;
        printf("[init] heartbeat %d\n", i);
    }

    return 0;
}
