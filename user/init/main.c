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
 */

#include "early_console.h"
#include "svc_manager.h"

#include <core_services.h>
#include <libs/serial/serial.h>
#include <module_index.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

/* 服务管理器 */
static struct svc_manager g_mgr;

/* 用户配置路径(可通过引导参数覆盖) */
static const char *g_user_config_path = USER_CONFIG_DEFAULT;

static void drain_ready_notifications(handle_t init_notify_ep) {
    if (init_notify_ep == HANDLE_INVALID) {
        return;
    }

    for (int i = 0; i < 16; i++) {
        struct ipc_message msg     = {0};
        char               buf[64] = {0};
        msg.buffer.data            = (uint64_t)(uintptr_t)buf;
        msg.buffer.size            = sizeof(buf);

        int ipc_ret = sys_ipc_receive((uint32_t)init_notify_ep, &msg, 1);
        if (ipc_ret != 0) {
            break;
        }

        svc_handle_ready_notification(&g_mgr, &msg);
        if (msg.sender_tid != 0xFFFFFFFFu) {
            struct ipc_message reply = {0};
            reply.regs.data[0]       = 0;
            sys_ipc_reply_to(msg.sender_tid, &reply);
        }
    }
}

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
    /* 使用早期控制台输出(seriald 启动前)*/
    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("init started\n");

    /* 解析启动参数 */
    if (argc > 0) {
        parse_args(argc, argv);
    }

    /* 初始化服务管理器 */
    svc_manager_init(&g_mgr);

    /* 加载嵌入式核心服务配置 */
    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("loading core services config...\n");
    int ret = svc_load_config_string(&g_mgr, CORE_SERVICES_CONF);
    if (ret < 0) {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_set_color(12, 0);
        early_puts("FATAL");
        early_reset_color();
        early_puts(": failed to load core services\n");
        while (1) {
            msleep(1000);
        }
    }

    {
        char buf[64];
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        snprintf(buf, sizeof(buf), "loaded %d services, graph_valid=%d\n", g_mgr.count,
                 g_mgr.graph_valid);
        early_puts(buf);
    }

    /* 初始化 VFS 客户端(vfs_ep 在 core_services.conf 中定义为 endpoint handle) */
    handle_t vfs_ep = sys_handle_find("vfs_ep");
    if (vfs_ep != HANDLE_INVALID) {
        vfs_client_init((uint32_t)vfs_ep);
    } else {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_set_color(14, 0);
        early_puts("WARNING");
        early_reset_color();
        early_puts(": vfs_ep handle not found, VFS client disabled\n");
    }

    /* 创建 init_notify endpoint 用于接收服务就绪通知 */
    handle_t init_notify_ep = sys_endpoint_create("init_notify");
    if (init_notify_ep == HANDLE_INVALID) {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_puts("failed to create init_notify endpoint\n");
        g_mgr.init_notify_ep = HANDLE_INVALID;
    } else {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_puts("init_notify endpoint created\n");
        g_mgr.init_notify_ep = (handle_t)init_notify_ep;
    }

    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("entering main loop\n");

    /* 主循环:先启动核心服务,等待 /sys 可用后加载用户配置 */
    bool serial_initialized = false;
    int  loop_count         = 0;

    while (1) {
        /* 收割子进程 */
        reap_children();

        drain_ready_notifications(init_notify_ep);

        /* 服务管理器 tick (并行调度) */
        if (g_mgr.graph_valid) {
            svc_tick_parallel(&g_mgr);
        } else {
            svc_tick(&g_mgr); /* 回退到旧的 tick */
        }

        drain_ready_notifications(init_notify_ep);

        /* 前几次 tick 输出诊断 */
        if (loop_count < 5 && early_console_is_active()) {
            char buf[80];
            early_set_color(10, 0);
            early_puts("[INIT] ");
            early_reset_color();
            snprintf(buf, sizeof(buf), "tick %d:", loop_count);
            early_puts(buf);
            for (int i = 0; i < g_mgr.count; i++) {
                snprintf(buf, sizeof(buf), " %s=%d", g_mgr.configs[i].name, g_mgr.runtime[i].state);
                early_puts(buf);
            }
            early_puts("\n");
        }

        /* 检查是否可以切换到 IPC-based 输出 (ttyd 就绪后) */
        if (!serial_initialized && early_console_is_active()) {
            int ttyd_idx = svc_find_by_name(&g_mgr, "ttyd");
            if (ttyd_idx >= 0 && g_mgr.runtime[ttyd_idx].ready) {
                early_puts("[INIT] ttyd ready - keeping early console for debugging\n");

                /* TODO: 暂时不切换到 tty1,因为会触发内核崩溃
                 * 问题:切换后 printf 通过 IPC 发送到 ttyd,内核在处理时访问无效内存
                 */
                serial_init();
                serial_initialized = true;

                early_puts("[INIT] system ready (still using early console)\n");
            }
        }

        /* 尝试加载用户配置(在 /sys 挂载后) */
        /* TODO: Re-enable when VFS service is implemented */
        /*
        if (!user_config_loaded && serial_initialized) {
            int test_fd = vfs_open(g_user_config_path, 0);
            if (test_fd >= 0) {
                vfs_close(test_fd);
                ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[INIT] ", " loading user config from
        %s\n", g_user_config_path); ret = svc_load_config(&g_mgr, g_user_config_path); if (ret == 0)
        { ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[INIT] ", " user config loaded\n"); } else {
                    ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[INIT] ", " user config load failed:
        %d\n", ret);
                }
                user_config_loaded = true;
            }
        }
        */
        loop_count++;
        msleep(50);
    }

    return 0;
}
