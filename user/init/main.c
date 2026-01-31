/**
 * @file init.c
 * @brief 用户态 init 进程
 *
 * init 是第一个用户进程,负责启动系统服务.
 *
 * 内核传递的 cap:
 *   handle 0: serial_ep (串口 endpoint)
 *   handle 1: io_cap (I/O 端口 capability)
 *   handle 2: vfs_ep (VFS endpoint, 传给 ramfsd)
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/syscall.h>

/* 自动生成的模块索引(按字母序排列驱动) */
#include <module_index.h>

/* init 继承的 capability handles */
#define CAP_SERIAL_EP 0
#define CAP_IOPORT    1
#define CAP_VFS_EP    2

/* 需要保活的服务 PID */
static int shell_pid = -1;

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

static void start_ramfsd(void) {
    printf("[init] Starting ramfsd...\n");

    struct spawn_args args = {
        .name         = "ramfsd",
        .module_index = MODULE_RAMFSD,
        .cap_count    = 1,
        .caps =
            {
                /* 传递 vfs_ep 给 ramfsd (handle 0) */
                {.src = CAP_VFS_EP, .rights = CAP_READ | CAP_WRITE, .dst_hint = 0},
            },
    };

    int pid = sys_spawn(&args);
    if (pid < 0) {
        printf("[init] Failed to start ramfsd: %d\n", pid);
        return;
    }
    printf("[init] ramfsd started (pid=%d)\n", pid);

    /* 等待 ramfsd 初始化 */
    msleep(100);

    /* 挂载根文件系统 */
    int ret = sys_mount("/", CAP_VFS_EP);
    if (ret < 0) {
        printf("[init] Failed to mount root filesystem: %d\n", ret);
    } else {
        printf("[init] Root filesystem mounted\n");
    }
}

static int spawn_shell(void) {
    struct spawn_args args = {
        .name         = "shell",
        .module_index = MODULE_SHELL,
        .cap_count    = 0,
    };
    return sys_spawn(&args);
}

static void start_shell(void) {
    printf("[init] Starting shell...\n");

    int pid = spawn_shell();
    if (pid < 0) {
        printf("[init] Failed to start shell: %d\n", pid);
    } else {
        printf("[init] shell started (pid=%d)\n", pid);
        shell_pid = pid;
    }
}


static void reap_children(void) {
    int status;
    int pid;

    /* 非阻塞收割所有已退出的子进程 */
    while ((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[init] Reaped child process %d (status=%d)\n", pid, status);

        /* shell 退出后自动重启 */
        if (pid == shell_pid) {
            printf("[init] Shell exited, respawning...\n");
            int new_pid = spawn_shell();
            if (new_pid < 0) {
                printf("[init] Failed to respawn shell: %d\n", new_pid);
                shell_pid = -1;
            } else {
                printf("[init] shell respawned (pid=%d)\n", new_pid);
                shell_pid = new_pid;
            }
        }
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

    /* 启动 ramfsd 并挂载根文件系统 */
    start_ramfsd();

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
