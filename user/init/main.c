/**
 * @file init.c
 * @brief 用户态 init 进程
 *
 * init 是第一个用户进程,负责启动系统服务.
 *
 * 启动流程:
 *   1. 启动内置 ramfsd 服务线程
 *   2. 提取 initramfs.img 到 ramfs
 *   3. 从 ramfs 加载 /etc/sys.conf (统一服务配置)
 *   4. ramfs:// 路径服务通过 bootstrap 从 ramfs 启动(绕过 VFS)
 *   5. vfsserver ready -> 迁移到 vfsserver, mount ramfs at "/"
 *   6. fatfsd ready -> vfs_mount("/", fatfs_ep), vfsserver 执行 remount
 *      从此所有 VFS 路径解析走 fatfsd (system.img 由 fatfsd 自行挂载)
 *   7. 普通路径服务通过 VFS sys_exec 启动
 *   8. 系统就绪 (shell 可用)
 *
 * init 永久使用 DEBUG 通道 (SYS_DEBUG_WRITE) 输出,
 * 不依赖任何 IPC 服务,始终可用.
 */

#include "initramfs.h"
#include "ramfs.h"
#include "ramfsd_service.h"
#include "svc_manager.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/cap.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

#include "bootstrap/bootstrap.h"

/* 服务管理器 */
static struct svc_manager g_mgr;

/* 内置 ramfsd 服务(需要被 svc_runtime.c 访问) */
struct ramfsd_service g_ramfsd;


static void drain_ready_notifications(handle_t init_notify_ep) {
    if (init_notify_ep == HANDLE_INVALID) {
        return;
    }

    for (int i = 0; i < 16; i++) {
        struct ipc_message msg     = {0};
        char               buf[64] = {0};
        msg.buffer.data            = (uint64_t)(uintptr_t)buf;
        msg.buffer.size            = sizeof(buf);

        int ipc_ret = sys_ipc_receive((uint32_t)init_notify_ep, &msg, 10);
        if (ipc_ret != 0) {
            if (errno == EPERM) {
                printf("[INIT] ERROR: init_notify recv denied (EPERM)\n");
            }
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
 */
static void parse_args(int argc, char **argv) {
    (void)argc;
    (void)argv;
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

    printf("[INIT] init started\n");

    /* 解析启动参数 */
    if (argc > 0) {
        parse_args(argc, argv);
    }

    /* 初始化服务管理器 */
    svc_manager_init(&g_mgr);

    /* 启动内置 ramfsd 服务 */
    printf("[INIT] starting ramfsd service...\n");

    ret = ramfsd_service_start(&g_ramfsd);
    if (ret < 0) {
        printf("[INIT] FATAL: failed to start ramfsd\n");
        while (1) {
            msleep(1000);
        }
    }

    /* 等待 ramfsd 线程启动 */
    msleep(50);

    /* 提取 initramfs.img 到 ramfs */
    printf("[INIT] extracting initramfs...\n");

    handle_t initramfs_h = sys_handle_find("boot.initramfs");
    if (initramfs_h == HANDLE_INVALID) {
        printf("[INIT] FATAL: initramfs module not found\n");
        while (1) {
            msleep(1000);
        }
    }

    uint32_t initramfs_size = 0;
    void    *initramfs_addr = sys_mmap_phys(initramfs_h, 0, 0, 0x03, &initramfs_size);
    if (initramfs_addr == NULL || (intptr_t)initramfs_addr < 0) {
        printf("[INIT] FATAL: failed to map initramfs\n");
        while (1) {
            msleep(1000);
        }
    }

    printf("[INIT] initramfs mapped at %p, size %u bytes\n", initramfs_addr, initramfs_size);

    struct ramfs_ctx *ramfs = ramfsd_service_get_ramfs(&g_ramfsd);
    ret                     = initramfs_extract(ramfs, initramfs_addr, initramfs_size);
    if (ret < 0) {
        printf("[INIT] FATAL: failed to extract initramfs\n");
        while (1) {
            msleep(1000);
        }
    }

    printf("[INIT] initramfs extracted successfully\n");

    /* 从 ramfs 加载核心服务配置 */
    printf("[INIT] loading system config from ramfs...\n");

    int config_fd = ramfs_open(ramfs, "/etc/sys.conf", VFS_O_RDONLY);
    if (config_fd < 0) {
        printf("[INIT] FATAL: failed to open /etc/sys.conf from ramfs\n");
        while (1) {
            msleep(1000);
        }
    }

    struct vfs_info info;
    ret = ramfs_finfo(ramfs, config_fd, &info);
    if (ret < 0) {
        printf("[INIT] FATAL: failed to get config file info\n");
        while (1) {
            msleep(1000);
        }
    }

    char *config_buf = malloc(info.size + 1);
    if (!config_buf) {
        printf("[INIT] FATAL: out of memory\n");
        while (1) {
            msleep(1000);
        }
    }

    ret = ramfs_read(ramfs, config_fd, config_buf, 0, info.size);
    if (ret < 0) {
        printf("[INIT] FATAL: failed to read config file\n");
        while (1) {
            msleep(1000);
        }
    }
    config_buf[info.size] = '\0';
    ramfs_close(ramfs, config_fd);

    ret = svc_load_config_string(&g_mgr, config_buf);
    free(config_buf);
    if (ret < 0) {
        printf("[INIT] FATAL: failed to load system config\n");
        while (1) {
            msleep(1000);
        }
    }

    printf("[INIT] loaded %d services, graph_valid=%d\n", g_mgr.count, g_mgr.graph_valid);

    /* 初始化 VFS 客户端(直连 ramfsd) */
    printf("[INIT] initializing VFS client (direct mode)...\n");

    handle_t ramfs_ep = g_ramfsd.endpoint;
    if (ramfs_ep != HANDLE_INVALID) {
        vfs_client_init((uint32_t)ramfs_ep);
        printf("[INIT] VFS client initialized (ramfsd direct mode)\n");
    } else {
        printf("[INIT] FATAL: ramfs_ep is invalid\n");
        while (1) {
            msleep(1000);
        }
    }

    /* 创建 init_notify endpoint */
    handle_t init_notify_ep = sys_endpoint_create("init_notify");
    if (init_notify_ep == HANDLE_INVALID) {
        printf("[INIT] failed to create init_notify endpoint\n");
        g_mgr.init_notify_ep = HANDLE_INVALID;
    } else {
        printf("[INIT] init_notify endpoint created\n");
        g_mgr.init_notify_ep = (handle_t)init_notify_ep;
    }

    printf("[INIT] entering main loop\n");

    /* 主循环 */
    static bool vfsserver_migrated = false;
    bool        system_ready       = false;
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
                    printf("[INIT] migrating to vfsserver...\n");

                    vfs_client_init((uint32_t)vfs_ep);

                    handle_t ramfs_ep = g_ramfsd.endpoint;
                    if (ramfs_ep != HANDLE_INVALID) {
                        ret = vfs_mount("/", ramfs_ep);
                        if (ret < 0) {
                            printf("[INIT] WARNING: failed to mount ramfsd via vfsserver\n");
                        } else {
                            printf("[INIT] ramfsd mounted at / via vfsserver\n");
                        }
                    }
                    vfsserver_migrated = true;
                }
            }
        }

        /* 所有服务就绪后输出 system ready */
        if (!system_ready) {
            bool all_ready = true;
            for (int i = 0; i < g_mgr.count; i++) {
                svc_state_t s = g_mgr.runtime[i].state;
                if (s != SVC_STATE_RUNNING && s != SVC_STATE_STOPPED && s != SVC_STATE_FAILED) {
                    all_ready = false;
                    break;
                }
            }
            if (all_ready && g_mgr.count > 0) {
                printf("[INIT] system ready\n");
                system_ready = true;
            }
        }

        /* 诊断输出(前 5 个 tick) */
        if (loop_count < 5) {
            char buf[256];
            int  pos = snprintf(buf, sizeof(buf), "[INIT] tick %d:", loop_count);
            for (int i = 0; i < g_mgr.count && pos < (int)sizeof(buf) - 20; i++) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, " %s=%d",
                                g_mgr.configs[i].name, g_mgr.runtime[i].state);
            }
            printf("%s\n", buf);
        }

        loop_count++;
        msleep(50);
    }

    return 0;
}
