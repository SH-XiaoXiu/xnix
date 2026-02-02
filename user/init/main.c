/**
 * @file init.c
 * @brief 用户态 init 进程
 *
 * init 是第一个用户进程,负责启动系统服务.
 * 采用两阶段启动模式:
 *   1. 硬编码阶段:启动核心服务(seriald, fbd, ramfsd, fatfsd)
 *   2. 配置阶段:从 /mnt/etc/services.conf 加载服务配置
 *
 * 内核传递的 cap:
 *   handle 0: serial_ep (串口 endpoint)
 *   handle 1: io_cap (I/O 端口 capability, 传给 seriald)
 *   handle 2: vfs_ep (VFS endpoint, 传给 ramfsd)
 *   handle 3: ata_io_cap (ATA 数据端口, 传给 fatfsd)
 *   handle 4: ata_ctrl_cap (ATA 控制端口, 传给 fatfsd)
 *   handle 5: fat_vfs_ep (FAT VFS endpoint, 传给 fatfsd)
 *   handle 6: fb_ep (Framebuffer endpoint, 传给 fbd)
 */

#include "svc_manager.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/syscall.h>

/* 自动生成的模块索引(按字母序排列驱动) */
#include <module_index.h>

/* init 继承的 capability handles */
#define CAP_SERIAL_EP  0
#define CAP_IOPORT     1
#define CAP_VFS_EP     2
#define CAP_ATA_IO     3
#define CAP_ATA_CTRL   4
#define CAP_FAT_VFS_EP 5
#define CAP_FB_EP      6

/* 服务配置文件路径 */
#define SVC_CONFIG_PATH "/mnt/etc/services.conf"

/* 服务管理器 */
static struct svc_manager g_mgr;

/* 核心服务 PID(用于标记内置服务) */
static int seriald_pid = -1;
static int ramfsd_pid  = -1;
static int fatfsd_pid  = -1;
static int fbd_pid     = -1;

/* 回退模式下的 shell PID */
static int  shell_pid     = -1;
static bool fallback_mode = false;

static int start_seriald(void) {
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
    return pid;
}

static int start_kbd(void) {
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
    return pid;
}

static int start_fbd(void) {
    printf("[init] Starting fbd...\n");

    struct spawn_args args = {
        .name         = "fbd",
        .module_index = MODULE_FBD,
        .cap_count    = 1,
        .caps =
            {
                /* 传递 fb_ep 给 fbd (handle 0) */
                {.src = CAP_FB_EP, .rights = CAP_READ | CAP_WRITE, .dst_hint = 0},
            },
    };

    int pid = sys_spawn(&args);
    if (pid < 0) {
        printf("[init] Failed to start fbd: %d\n", pid);
    } else {
        printf("[init] fbd started (pid=%d)\n", pid);
    }
    return pid;
}

static int start_ramfsd(void) {
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
        return pid;
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

    return pid;
}

static int start_fatfsd(void) {
    printf("[init] Starting fatfsd...\n");

    struct spawn_args args = {
        .name         = "fatfsd",
        .module_index = MODULE_FATFSD,
        .cap_count    = 3,
        .caps =
            {
                /* 传递 fat_vfs_ep 给 fatfsd (handle 0) */
                {.src = CAP_FAT_VFS_EP, .rights = CAP_READ | CAP_WRITE, .dst_hint = 0},
                /* 传递 ata_io_cap 给 fatfsd (handle 1) */
                {.src = CAP_ATA_IO, .rights = CAP_READ | CAP_WRITE, .dst_hint = 1},
                /* 传递 ata_ctrl_cap 给 fatfsd (handle 2) */
                {.src = CAP_ATA_CTRL, .rights = CAP_READ | CAP_WRITE, .dst_hint = 2},
            },
    };

    int pid = sys_spawn(&args);
    if (pid < 0) {
        printf("[init] Failed to start fatfsd: %d\n", pid);
        return pid;
    }
    printf("[init] fatfsd started (pid=%d)\n", pid);

    /* 等待 fatfsd 初始化 */
    msleep(200);

    /* 挂载 FAT 文件系统到 /mnt */
    int ret = sys_mount("/mnt", CAP_FAT_VFS_EP);
    if (ret < 0) {
        printf("[init] Failed to mount FAT filesystem: %d\n", ret);
    } else {
        printf("[init] FAT filesystem mounted at /mnt\n");
    }

    return pid;
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
        if (fallback_mode) {
            printf("[init] Reaped child process %d (status=%d)\n", pid, status);

            /* shell 退出后自动重启(回退模式) */
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
        } else {
            /* 交给服务管理器处理 */
            svc_handle_exit(&g_mgr, pid, status);
        }
    }
}

/**
 * 硬编码启动阶段
 * 启动核心服务:seriald → fbd → ramfsd → fatfsd → 挂载 /mnt
 */
static void boot_phase_hardcoded(void) {
    /* 启动 seriald 服务 */
    seriald_pid = start_seriald();

    /* 等待 seriald 初始化 */
    sleep(1);

    /* 启动 fbd 服务 */
    fbd_pid = start_fbd();

    /* 启动 ramfsd 并挂载根文件系统 */
    ramfsd_pid = start_ramfsd();

    /* 启动 fatfsd 并挂载到 /mnt */
    fatfsd_pid = start_fatfsd();
}

/**
 * 配置启动阶段
 * 加载服务配置,使用服务管理器启动剩余服务
 */
static bool boot_phase_config(void) {
    /* 初始化服务管理器 */
    svc_manager_init(&g_mgr);

    /* 尝试加载配置文件 */
    int ret = svc_load_config(&g_mgr, SVC_CONFIG_PATH);
    if (ret < 0) {
        printf("[init] Failed to load %s, using fallback\n", SVC_CONFIG_PATH);
        return false;
    }

    /* 标记内置服务已启动 */
    if (seriald_pid > 0) {
        svc_mark_builtin(&g_mgr, "seriald", seriald_pid);
    }
    if (fbd_pid > 0) {
        svc_mark_builtin(&g_mgr, "fbd", fbd_pid);
    }
    if (ramfsd_pid > 0) {
        svc_mark_builtin(&g_mgr, "ramfsd", ramfsd_pid);
    }
    if (fatfsd_pid > 0) {
        svc_mark_builtin(&g_mgr, "fatfsd", fatfsd_pid);
    }

    return true;
}

/**
 * 回退启动
 * 配置加载失败时使用硬编码方式启动服务
 */
static void boot_fallback(void) {
    printf("[init] Using fallback startup\n");
    fallback_mode = true;

    /* 启动 kbd 服务 */
    start_kbd();

    /* 启动 shell */
    start_shell();
}

int main(void) {
    printf("[init] init process started (PID %d)\n", sys_getpid());

    /* 阶段 1:硬编码启动核心服务 */
    boot_phase_hardcoded();

    /* 阶段 2:尝试配置驱动启动 */
    bool config_ok = boot_phase_config();

    if (!config_ok) {
        /* 回退到硬编码启动 */
        boot_fallback();
    }

    printf("[init] System ready\n");

    /* 主循环 */
    while (1) {
        /* 收割子进程 */
        reap_children();

        /* 服务管理器 tick(仅在配置模式) */
        if (!fallback_mode) {
            svc_tick(&g_mgr);
        }

        msleep(100);
    }

    return 0;
}
