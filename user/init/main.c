/**
 * @file init.c
 * @brief 用户态 init 进程
 *
 * init 是第一个用户进程,负责启动系统服务.
 *
 * 启动流程:
 *   1. 加载嵌入式核心服务配置(编译时生成)
 *   2. 启动核心服务(seriald, ramfsd, rootfsd)
 *   3. 等待 /sys 挂载完成
 *   4. 加载用户配置(可通过引导参数 config=xxx 覆盖)
 *   5. 按配置启动用户服务
 *
 * 内核传递的 capability handles(按名称映射):
 *   serial_ep, ioport, vfs_ep, ata_io, ata_ctrl, fat_vfs_ep, fb_ep, rootfs_ep
 */

#include "svc_manager.h"

#include <acolor.h>
#include <core_services.h>
#include <module_index.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xnix/syscall.h>
#include <xnix/udm/vfs.h>

/* 服务管理器 */
static struct svc_manager g_mgr;

/* 用户配置路径(可通过引导参数覆盖) */
static const char *g_user_config_path = USER_CONFIG_DEFAULT;

/**
 * 解析启动参数
 * 支持: config=<path>
 */
static void parse_args(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "config=", 7) == 0) {
            g_user_config_path = argv[i] + 7;
            printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " User config: %s\n", g_user_config_path);
        }
    }
}

/**
 * 收割退出的子进程
 */
static void reap_children(void) {
    int status;
    int pid;

    while ((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
        svc_handle_exit(&g_mgr, pid, status);
    }
}

int main(int argc, char **argv) {
    printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " init process started (PID %d)\n", sys_getpid());

    /* 解析启动参数 */
    if (argc > 0) {
        parse_args(argc, argv);
    }

    /* 初始化服务管理器 */
    svc_manager_init(&g_mgr);

    /* 加载嵌入式核心服务配置 */
    printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " Loading embedded core services config\n");
    int ret = svc_load_config_string(&g_mgr, CORE_SERVICES_CONF);
    if (ret < 0) {
        printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " Failed to load core services: %d\n", ret);
        printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " System cannot start without core services\n");
        while (1) {
            msleep(1000);
        }
    }

    printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " Starting core services...\n");

    /* 主循环:先启动核心服务,等待 /sys 可用后加载用户配置 */
    bool user_config_loaded = false;

    while (1) {
        /* 收割子进程 */
        reap_children();

        /* 服务管理器 tick */
        svc_tick(&g_mgr);

        /* 尝试加载用户配置(在 /sys 挂载后) */
        if (!user_config_loaded) {
            struct vfs_info info;
            if (sys_info(g_user_config_path, &info) == 0) {
                printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " Loading %s\n", g_user_config_path);
                ret = svc_load_config(&g_mgr, g_user_config_path);
                if (ret < 0) {
                    printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " User config load failed: %d\n",
                           ret);
                }
                user_config_loaded = true;
            }
        }

        msleep(50);
    }

    return 0;
}
