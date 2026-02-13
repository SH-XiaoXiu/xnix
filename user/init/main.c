/**
 * @file init.c
 * @brief 用户态 init 进程
 *
 * init 是第一个用户进程,负责启动系统服务.
 *
 * 启动流程:
 *   1. 启动内置 ramfsd 服务线程
 *   2. 提取 initramfs.img 到 ramfs
 *   3. 从 ramfs 加载核心服务配置
 *   4. 使用 bootstrap 从 ramfs 启动核心服务(绕过 VFS)
 *   5. vfsserver ready -> 迁移到 vfsserver, mount ramfs at "/"
 *   6. fatfsd ready -> vfs_mount("/", fatfs_ep), vfsserver 执行 remount
 *      从此所有 VFS 路径解析走 fatfsd (system.img 由 fatfsd 自行挂载)
 *   7. 从 VFS 读 /etc/user_services.conf -> 启动用户服务
 *   8. 系统就绪 (shell 可用)
 */

#include "early_console.h"
#include "initramfs.h"
#include "ramfs.h"
#include "ramfsd_service.h"
#include "svc_manager.h"

#include <libs/serial/serial.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/perm.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

#include "bootstrap/bootstrap.h"

/* 服务管理器 */
static struct svc_manager g_mgr;

/* 内置 ramfsd 服务(需要被 svc_runtime.c 访问)*/
struct ramfsd_service g_ramfsd;

/* 用户配置路径(可通过引导参数覆盖) */
static const char *g_user_config_path = "/etc/user_services.conf";

/**
 * 从管理器的 profile 配置创建内核 profile
 */
static void create_profiles_from_config(struct svc_manager *mgr) {
    for (int i = 0; i < mgr->profile_count; i++) {
        struct svc_profile *prof = &mgr->profiles[i];

        struct abi_profile_create_args args;
        memset(&args, 0, sizeof(args));
        strncpy(args.name, prof->name, sizeof(args.name) - 1);
        if (prof->inherit[0] != '\0') {
            strncpy(args.parent, prof->inherit, sizeof(args.parent) - 1);
        }

        args.rule_count = 0;
        for (int j = 0; j < prof->perm_count && args.rule_count < ABI_PERM_RULE_MAX; j++) {
            strncpy(args.rules[args.rule_count].node, prof->perms[j].name, ABI_PERM_NODE_MAX - 1);
            args.rules[args.rule_count].value = prof->perms[j].value ? 1 : 0;
            args.rule_count++;
        }

        int ret = sys_perm_profile_create(&args);
        if (ret < 0) {
            char buf[80];
            early_set_color(10, 0);
            early_puts("[INIT] ");
            early_reset_color();
            snprintf(buf, sizeof(buf), "WARNING: profile '%s' create failed (%d)\n", prof->name,
                     ret);
            early_puts(buf);
        } else {
            char buf[64];
            early_set_color(10, 0);
            early_puts("[INIT] ");
            early_reset_color();
            snprintf(buf, sizeof(buf), "profile '%s' created\n", prof->name);
            early_puts(buf);
        }
    }
}

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
    int ret;

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

    /* 启动内置 ramfsd 服务 */
    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("starting ramfsd service...\n");

    ret = ramfsd_service_start(&g_ramfsd);
    if (ret < 0) {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_set_color(12, 0);
        early_puts("FATAL");
        early_reset_color();
        early_puts(": failed to start ramfsd\n");
        while (1) {
            msleep(1000);
        }
    }

    /* 等待 ramfsd 线程启动 */
    msleep(50);

    /* 提取 initramfs.img 到 ramfs */
    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("extracting initramfs...\n");

    handle_t initramfs_h = sys_handle_find("boot.initramfs");
    if (initramfs_h == HANDLE_INVALID) {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_set_color(12, 0);
        early_puts("FATAL");
        early_reset_color();
        early_puts(": initramfs module not found\n");
        while (1) {
            msleep(1000);
        }
    }

    uint32_t initramfs_size = 0;
    void    *initramfs_addr = sys_mmap_phys(initramfs_h, 0, 0, 0x03, &initramfs_size);
    if (initramfs_addr == NULL || (intptr_t)initramfs_addr < 0) {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_set_color(12, 0);
        early_puts("FATAL");
        early_reset_color();
        early_puts(": failed to map initramfs\n");
        while (1) {
            msleep(1000);
        }
    }

    {
        char buf[80];
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        snprintf(buf, sizeof(buf), "initramfs mapped at %p, size %u bytes\n", initramfs_addr,
                 initramfs_size);
        early_puts(buf);
    }

    struct ramfs_ctx *ramfs = ramfsd_service_get_ramfs(&g_ramfsd);
    ret                     = initramfs_extract(ramfs, initramfs_addr, initramfs_size);
    if (ret < 0) {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_set_color(12, 0);
        early_puts("FATAL");
        early_reset_color();
        early_puts(": failed to extract initramfs\n");
        while (1) {
            msleep(1000);
        }
    }

    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("initramfs extracted successfully\n");

    /* 从 ramfs 加载核心服务配置 */
    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("loading core services config from ramfs...\n");

    int config_fd = ramfs_open(ramfs, "/etc/core_services.conf", VFS_O_RDONLY);
    if (config_fd < 0) {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_set_color(12, 0);
        early_puts("FATAL");
        early_reset_color();
        early_puts(": failed to open /etc/core_services.conf from ramfs\n");
        while (1) {
            msleep(1000);
        }
    }

    struct vfs_info info;
    ret = ramfs_finfo(ramfs, config_fd, &info);
    if (ret < 0) {
        early_puts("[INIT] FATAL: failed to get config file info\n");
        while (1) {
            msleep(1000);
        }
    }

    char *config_buf = malloc(info.size + 1);
    if (!config_buf) {
        early_puts("[INIT] FATAL: out of memory\n");
        while (1) {
            msleep(1000);
        }
    }

    ret = ramfs_read(ramfs, config_fd, config_buf, 0, info.size);
    if (ret < 0) {
        early_puts("[INIT] FATAL: failed to read config file\n");
        while (1) {
            msleep(1000);
        }
    }
    config_buf[info.size] = '\0';
    ramfs_close(ramfs, config_fd);

    ret = svc_load_config_string(&g_mgr, config_buf);
    free(config_buf);
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

    /* 在启动服务之前,通过 syscall 在内核中注册 profile */
    create_profiles_from_config(&g_mgr);

    /* 初始化 VFS 客户端(直连 ramfsd) */
    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("initializing VFS client (direct mode)...\n");

    handle_t ramfs_ep = g_ramfsd.endpoint;
    if (ramfs_ep != HANDLE_INVALID) {
        vfs_client_init((uint32_t)ramfs_ep);
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_puts("VFS client initialized (ramfsd direct mode)\n");
    } else {
        early_puts("[INIT] FATAL: ramfs_ep is invalid\n");
        while (1) {
            msleep(1000);
        }
    }

    /* 创建 init_notify endpoint */
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

    /* 主循环 */
    static bool user_config_loaded = false;
    static bool vfsserver_migrated = false;
    bool        serial_initialized = false;
    int         loop_count         = 0;

    while (1) {
        reap_children();

        drain_ready_notifications(init_notify_ep);

        if (g_mgr.graph_valid) {
            svc_tick_parallel(&g_mgr);
        } else {
            svc_tick(&g_mgr);
        }

        drain_ready_notifications(init_notify_ep);

        /* vfsserver 就绪后,迁移到 vfsserver 并挂载 ramfsd 到 "/" */
        if (!vfsserver_migrated) {
            int vfsserver_idx = svc_find_by_name(&g_mgr, "vfsserver");
            if (vfsserver_idx >= 0 && g_mgr.runtime[vfsserver_idx].ready) {
                handle_t vfs_ep = sys_handle_find("vfs_ep");
                if (vfs_ep != HANDLE_INVALID) {
                    early_set_color(10, 0);
                    early_puts("[INIT] ");
                    early_reset_color();
                    early_puts("migrating to vfsserver...\n");

                    vfs_client_init((uint32_t)vfs_ep);

                    handle_t ramfs_ep = g_ramfsd.endpoint;
                    if (ramfs_ep != HANDLE_INVALID) {
                        ret = vfs_mount("/", ramfs_ep);
                        if (ret < 0) {
                            early_puts("[INIT] WARNING: failed to mount ramfsd via vfsserver\n");
                        } else {
                            early_set_color(10, 0);
                            early_puts("[INIT] ");
                            early_reset_color();
                            early_puts("ramfsd mounted at / via vfsserver\n");
                        }
                    }
                    vfsserver_migrated = true;
                }
            }
        }

        /* ttyd 就绪后切换到 serial stdio */
        if (!serial_initialized) {
            int ttyd_idx = svc_find_by_name(&g_mgr, "ttyd");
            if (ttyd_idx >= 0 && g_mgr.runtime[ttyd_idx].ready) {
                early_puts("[INIT] system ready\n");
                serial_init();
                serial_initialized = true;
                early_console_disable();
            }
        }

        /* 诊断输出 */
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

        /*
         * 加载用户配置
         * fatfsd remount "/" 后, /etc/user_services.conf 从 system.img 可见
         */
        if (!user_config_loaded && serial_initialized) {
            int test_fd = vfs_open(g_user_config_path, 0);
            if (test_fd >= 0) {
                vfs_close(test_fd);
                ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[INIT] ",
                          "loading user config from %s\n", g_user_config_path);
                ret = svc_load_config(&g_mgr, g_user_config_path);
                if (ret == 0) {
                    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[INIT] ", "user config loaded\n");
                } else {
                    ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[INIT] ",
                              "user config load failed: %d\n", ret);
                }
                user_config_loaded = true;
            }
        }
        loop_count++;
        msleep(50);
    }

    return 0;
}
