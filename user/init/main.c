/**
 * @file init.c
 * @brief 用户态 init 进程
 *
 * init 是第一个用户进程,负责启动系统服务.
 *
 * 启动流程:
 *   1. 启动内置 ramfsd 服务线程
 *   2. 提取 initramfs.img 到 ramfs
 *   3. 映射 system.img (FAT32) 到内存
 *   4. 从 ramfs 加载核心服务配置
 *   5. 使用 bootstrap 从 system.img 启动服务(绕过 VFS)
 *   6. 等待 /sys 挂载完成
 *   7. 加载用户配置(可通过引导参数 config=xxx 覆盖)
 *   8. 按配置启动用户服务
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
#include <xnix/ipc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

#include "bootstrap/bootstrap.h"

/* 服务管理器 */
static struct svc_manager g_mgr;

/* 内置 ramfsd 服务(需要被 svc_runtime.c 访问)*/
struct ramfsd_service g_ramfsd;

/* 用户配置路径(可通过引导参数覆盖) */
static const char *g_user_config_path = "/sys/etc/user_services.conf";

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

    handle_t initramfs_h = sys_handle_find("module_initramfs");
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

    /* 映射 system.img (FAT32) */
    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("mapping system.img...\n");

    handle_t system_h = sys_handle_find("module_system");
    if (system_h == HANDLE_INVALID) {
        early_set_color(10, 0);
        early_puts("[INIT] ");
        early_reset_color();
        early_set_color(14, 0); /* 黄色 */
        early_puts("WARNING");
        early_reset_color();
        early_puts(": system.img module not found, bootstrap disabled\n");
        g_mgr.system_volume = NULL;
    } else {
        uint32_t system_size = 0;
        void    *system_addr = sys_mmap_phys(system_h, 0, 0, 0x03, &system_size);
        if (system_addr == NULL || (intptr_t)system_addr < 0) {
            early_set_color(10, 0);
            early_puts("[INIT] ");
            early_reset_color();
            early_set_color(14, 0);
            early_puts("WARNING");
            early_reset_color();
            early_puts(": failed to map system.img, bootstrap disabled\n");
            g_mgr.system_volume = NULL;
        } else {
            char buf[80];
            early_set_color(10, 0);
            early_puts("[INIT] ");
            early_reset_color();
            snprintf(buf, sizeof(buf), "system.img mapped at %p, size %u bytes\n", system_addr,
                     system_size);
            early_puts(buf);

            /* 挂载 FAT32 卷 */
            g_mgr.system_volume = fat32_mount(system_addr, system_size);
            if (g_mgr.system_volume == NULL) {
                early_set_color(10, 0);
                early_puts("[INIT] ");
                early_reset_color();
                early_set_color(14, 0);
                early_puts("WARNING");
                early_reset_color();
                early_puts(": failed to mount system.img as FAT32, bootstrap disabled\n");
            } else {
                early_set_color(10, 0);
                early_puts("[INIT] ");
                early_reset_color();
                early_puts("system.img mounted successfully\n");
            }
        }
    }

    /* 从 ramfs 加载核心服务配置 */
    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("loading core services config from ramfs...\n");

    /* 通过 ramfs 直接读取配置文件 */
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

    /* 获取文件大小 */
    struct vfs_info info;
    ret = ramfs_finfo(ramfs, config_fd, &info);
    if (ret < 0) {
        early_puts("[INIT] FATAL: failed to get config file info\n");
        while (1) {
            msleep(1000);
        }
    }

    /* 读取配置内容 */
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

    /* 加载配置 */
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

    /* 将 ramfsd 挂载到根目录 */
    early_set_color(10, 0);
    early_puts("[INIT] ");
    early_reset_color();
    early_puts("mounting ramfsd as root filesystem...\n");

    /* 首先需要初始化 VFS 客户端 - 使用内置的 ramfs_ep */
    handle_t vfs_ep = sys_handle_find("vfs_ep");
    if (vfs_ep == HANDLE_INVALID) {
        /* VFS 服务还没启动,先使用临时方案:直接挂载 ramfsd */
        handle_t ramfs_ep = g_ramfsd.endpoint;
        if (ramfs_ep != HANDLE_INVALID) {
            /* 初始化 VFS 客户端指向 ramfsd */
            vfs_client_init((uint32_t)ramfs_ep);

            /* 挂载 ramfsd 到根目录 */
            ret = vfs_mount("/", ramfs_ep);
            if (ret < 0) {
                early_puts("[INIT] FATAL: failed to mount ramfsd\n");
                while (1) {
                    msleep(1000);
                }
            }
            early_set_color(10, 0);
            early_puts("[INIT] ");
            early_reset_color();
            early_puts("ramfsd mounted at /\n");
        }
    } else {
        vfs_client_init((uint32_t)vfs_ep);
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
    static bool user_config_loaded = false;
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

        /* 检查是否可以关闭早期控制台(ttyd 就绪后,fbcond 已接管显示) */
        if (!serial_initialized) {
            int ttyd_idx = svc_find_by_name(&g_mgr, "ttyd");
            if (ttyd_idx >= 0 && g_mgr.runtime[ttyd_idx].ready) {
                early_puts("[INIT] system ready\n");
                serial_init();
                serial_initialized = true;
                early_console_disable();
            }
        }

        /* 前几次 tick 输出诊断(early console 关闭后自动停止) */
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

        /* 尝试加载用户配置(在 /sys 挂载后) */
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
