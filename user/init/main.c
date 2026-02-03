/**
 * @file init.c
 * @brief 用户态 init 进程
 *
 * init 是第一个用户进程,负责启动系统服务.
 *
 * 启动流程:
 *   1. 初始化 VFS 客户端
 *   2. 加载嵌入式核心服务配置(编译时生成)
 *   3. 启动核心服务(seriald, ramfsd, rootfsd)
 *   4. 等待 /sys 挂载完成
 *   5. 加载用户配置(可通过引导参数 config=xxx 覆盖)
 *   6. 按配置启动用户服务
 *
 * 内核传递的 capability handles(按名称映射):
 *   serial_ep, ioport, vfs_ep, ata_io, ata_ctrl, fat_vfs_ep, fb_ep, rootfs_ep
 */

#include "svc_manager.h"

#include <acolor.h>
#include <core_services.h>
#include <d/protocol/vfs.h>
#include <module_index.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/syscall.h>

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
    printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " init started (pid %d)\n", sys_getpid());

    /* 解析启动参数 */
    if (argc > 0) {
        parse_args(argc, argv);
    }

    /* 初始化服务管理器 */
    svc_manager_init(&g_mgr);

    /* 加载嵌入式核心服务配置 */
    int ret = svc_load_config_string(&g_mgr, CORE_SERVICES_CONF);
    if (ret < 0) {
        printf(ACOLOR_BRED "[INIT]" ACOLOR_RESET " failed to load core services\n");
        while (1) {
            msleep(1000);
        }
    }

    /* 动态获取 VFS endpoint 句柄 (从 g_mgr.cap_defs 中查找) */
    uint32_t vfs_ep_handle = 2; /* 默认回退 */
    for (int i = 0; i < g_mgr.cap_def_count; i++) {
        if (strcmp(g_mgr.cap_defs[i].name, "vfs_ep") == 0) {
            vfs_ep_handle = g_mgr.cap_defs[i].handle;
            break;
        }
    }

    /* 初始化 VFS 客户端 */
    vfs_client_init(vfs_ep_handle);
    printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " ready\n");

    /* 主循环:先启动核心服务,等待 /sys 可用后加载用户配置 */
    bool user_config_loaded = false;

    while (1) {
        /* 收割子进程 */
        reap_children();

        /* 服务管理器 tick */
        svc_tick(&g_mgr);

        /* 尝试加载用户配置(在 /sys 挂载后) */
        if (!user_config_loaded) {
            /* 检查配置文件是否存在(通过尝试打开) */
            int test_fd = vfs_open(g_user_config_path, 0);
            if (test_fd >= 0) {
                vfs_close(test_fd);
                printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " loading user config from %s\n",
                       g_user_config_path);
                ret = svc_load_config(&g_mgr, g_user_config_path);
                if (ret == 0) {
                    printf(ACOLOR_BGREEN "[INIT]" ACOLOR_RESET " user config loaded\n");
                } else {
                    printf(ACOLOR_BYELLOW "[INIT]" ACOLOR_RESET " user config load failed: %d\n",
                           ret);
                }
                user_config_loaded = true;
            }
        }

        msleep(50);
    }

    return 0;
}
