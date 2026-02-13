#include "early_console.h"
#include "ramfs.h"
#include "ramfsd_service.h"
#include "svc_internal.h"

#include <d/protocol/vfs.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/fd.h>
#include <xnix/ipc.h>
#include <xnix/proc.h>
#include <xnix/syscall.h>

/* 外部定义: init/main.c */
extern struct ramfsd_service g_ramfsd;

static bool probe_fs_ready(uint32_t ep, uint32_t timeout_ms) {
    uint32_t       elapsed        = 0;
    const uint32_t probe_interval = 50;

    while (elapsed < timeout_ms) {
        struct ipc_message msg   = {0};
        struct ipc_message reply = {0};

        msg.regs.data[0]      = UDM_VFS_INFO;
        const char *test_path = ".";
        msg.buffer.data       = (uint64_t)(uintptr_t)test_path;
        msg.buffer.size       = 2;

        int ret = sys_ipc_call(ep, &msg, &reply, 500);
        if (ret == 0) {
            return true;
        }

        msleep(probe_interval);
        elapsed += probe_interval;
    }

    return false;
}

static int do_mount(struct svc_config *cfg) {
    if (cfg->mount[0] == '\0') {
        return 0;
    }

    printf("Mounting %s on %s (ep=%u)\n", cfg->name, cfg->mount, cfg->mount_ep);
    int ret = vfs_mount(cfg->mount, cfg->mount_ep);
    if (ret < 0) {
        printf("Failed to mount %s: %s\n", cfg->mount, strerror(-ret));
    }
    return ret;
}

/**
 * 查找服务配置中的 tty handle,返回其 src_handle
 */
static uint32_t find_tty_handle(struct svc_config *cfg) {
    for (int i = 0; i < cfg->handle_count; i++) {
        if (strncmp(cfg->handles[i].name, "tty", 3) == 0 &&
            cfg->handles[i].src_handle != HANDLE_INVALID) {
            return cfg->handles[i].src_handle;
        }
    }
    return HANDLE_INVALID;
}

/**
 * 向 proc_image_builder 注入服务通用 handles
 */
static void inject_svc_handles_image(struct proc_image_builder *b, struct svc_manager *mgr,
                                     struct svc_config *cfg) {
    /* 配置中声明的 handles */
    for (int i = 0; i < cfg->handle_count; i++) {
        proc_image_add_handle(b, cfg->handles[i].src_handle, cfg->handles[i].name);
    }

    /* init_notify */
    if (mgr->init_notify_ep != HANDLE_INVALID) {
        proc_image_add_handle(b, mgr->init_notify_ep, "init_notify");
    }

    /* stdio → tty */
    uint32_t tty_h = find_tty_handle(cfg);
    if (tty_h != HANDLE_INVALID) {
        proc_image_add_handle(b, tty_h, HANDLE_STDIO_STDIN);
        proc_image_add_handle(b, tty_h, HANDLE_STDIO_STDOUT);
        proc_image_add_handle(b, tty_h, HANDLE_STDIO_STDERR);
    }
}

/**
 * 向 proc_builder 注入服务通用 handles
 */
static void inject_svc_handles(struct proc_builder *b, struct svc_manager *mgr,
                               struct svc_config *cfg) {
    /* 配置中声明的 handles */
    for (int i = 0; i < cfg->handle_count; i++) {
        proc_add_handle(b, cfg->handles[i].src_handle, cfg->handles[i].name);
    }

    /* init_notify */
    if (mgr->init_notify_ep != HANDLE_INVALID) {
        proc_add_handle(b, mgr->init_notify_ep, "init_notify");
    }

    /* stdio → tty */
    uint32_t tty_h = find_tty_handle(cfg);
    if (tty_h != HANDLE_INVALID) {
        proc_add_handle(b, tty_h, HANDLE_STDIO_STDIN);
        proc_add_handle(b, tty_h, HANDLE_STDIO_STDOUT);
        proc_add_handle(b, tty_h, HANDLE_STDIO_STDERR);
    }
}

/**
 * 从 ramfs 加载 ELF 并通过 proc_image_builder 启动(绕过 VFS)
 */
static int start_via_bootstrap(struct svc_manager *mgr, struct svc_config *cfg) {
    struct ramfs_ctx *ramfs = ramfsd_service_get_ramfs(&g_ramfsd);

    int fd = ramfs_open(ramfs, cfg->path, VFS_O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    struct vfs_info info;
    if (ramfs_finfo(ramfs, fd, &info) < 0) {
        ramfs_close(ramfs, fd);
        return -1;
    }

    void *elf_data = malloc(info.size);
    if (!elf_data) {
        ramfs_close(ramfs, fd);
        return -1;
    }

    int nread = ramfs_read(ramfs, fd, elf_data, 0, info.size);
    ramfs_close(ramfs, fd);
    if (nread < 0) {
        free(elf_data);
        return -1;
    }

    struct proc_image_builder b;
    proc_image_init(&b, cfg->name, elf_data, (size_t)nread);

    if (cfg->profile[0]) {
        proc_image_set_profile(&b, cfg->profile);
    }

    inject_svc_handles_image(&b, mgr, cfg);

    int pid = proc_image_spawn(&b);
    free(elf_data);
    return pid;
}

/**
 * 通过 VFS 路径启动(依赖 VFS 就绪)
 */
static int start_via_exec(struct svc_manager *mgr, struct svc_config *cfg) {
    struct proc_builder b;
    proc_init(&b, cfg->path);

    if (cfg->profile[0]) {
        proc_set_profile(&b, cfg->profile);
    }

    inject_svc_handles(&b, mgr, cfg);

    if (cfg->args[0]) {
        proc_add_args_string(&b, cfg->args);
    }

    return proc_spawn(&b);
}

int svc_start_service(struct svc_manager *mgr, int idx) {
    struct svc_config  *cfg = &mgr->configs[idx];
    struct svc_runtime *rt  = &mgr->runtime[idx];

    rt->state = SVC_STATE_STARTING;

    int pid;

    if (cfg->builtin) {
        /* 核心服务: 从 ramfs 加载 (bootstrap) */
        pid = start_via_bootstrap(mgr, cfg);
    } else {
        /* 用户服务: 通过 VFS 加载 (sys_exec) */
        pid = start_via_exec(mgr, cfg);
    }

    if (pid < 0) {
        if (early_console_is_active()) {
            char buf[160];
            snprintf(buf, sizeof(buf), "[INIT] ERROR: failed to start %s: %s\n", cfg->name,
                     strerror(-pid));
            early_puts(buf);
        } else {
            printf("Failed to start %s: %s\n", cfg->name, strerror(-pid));
        }
        rt->state = SVC_STATE_FAILED;
        return pid;
    }

    if (early_console_is_active()) {
        char buf[160];
        snprintf(buf, sizeof(buf), "[INIT] started %s (pid %d)\n", cfg->name, pid);
        early_puts(buf);
    } else {
        printf("%s started (pid=%d)\n", cfg->name, pid);
    }

    rt->state          = SVC_STATE_RUNNING;
    rt->pid            = pid;
    rt->start_ticks    = svc_get_ticks();
    rt->reported_ready = false;
    rt->mounted        = false;
    rt->ready          = false;

    if (strcmp(cfg->name, "shell") == 0) {
        /* shell 启动后,将 init 的 stdout/stderr 重定向到 serial (tty1),
         * 避免诊断输出干扰 shell 的 VGA 终端 (tty0). */
        handle_t log_ep = env_get_handle("tty1");
        if (log_ep != HANDLE_INVALID) {
            int log_fd = fd_alloc();
            if (log_fd >= 0) {
                fd_install(log_fd, log_ep, FD_TYPE_TTY, FD_FLAG_WRITE);
                dup2(log_fd, STDOUT_FILENO);
                dup2(log_fd, STDERR_FILENO);
                if (log_fd != STDOUT_FILENO && log_fd != STDERR_FILENO) {
                    /* 仅释放 fd 槽位,不关闭底层 kernel handle -
                     * tty1 handle 是服务管理器的共享资源,
                     * 后续启动的 shell_serial 等服务仍需使用它. */
                    fd_free(log_fd);
                }
            }
        }
    }

    return pid;
}

int svc_try_mount_service(struct svc_manager *mgr, int idx) {
    struct svc_config  *cfg = &mgr->configs[idx];
    struct svc_runtime *rt  = &mgr->runtime[idx];

    if (cfg->mount[0] == '\0') {
        return 0;
    }
    if (rt->state != SVC_STATE_RUNNING) {
        return 0;
    }
    if (!rt->reported_ready || rt->mounted) {
        return 0;
    }

    if (mgr->graph[idx].provides_count <= 0) {
        printf("ERROR: Service '%s' mount requires provides endpoint\n", cfg->name);
        rt->state = SVC_STATE_FAILED;
        return -1;
    }

    const char *ep_name = mgr->graph[idx].provides[0];
    cfg->mount_ep       = HANDLE_INVALID;
    for (int i = 0; i < cfg->handle_count; i++) {
        if (strcmp(cfg->handles[i].name, ep_name) == 0) {
            cfg->mount_ep = cfg->handles[i].src_handle;
            break;
        }
    }

    if (cfg->mount_ep == HANDLE_INVALID) {
        printf("ERROR: Service '%s' mount_ep is INVALID\n", cfg->name);
        rt->state = SVC_STATE_FAILED;
        return -1;
    }

    if (!probe_fs_ready(cfg->mount_ep, 5000)) {
        printf("Timeout: %s did not respond to probes\n", cfg->name);
        rt->state = SVC_STATE_FAILED;
        return -1;
    }

    int mount_ret = do_mount(cfg);
    if (mount_ret < 0) {
        rt->state = SVC_STATE_FAILED;
        return -1;
    }

    rt->mounted = true;
    rt->ready   = true;
    printf("%s mounted on %s\n", cfg->name, cfg->mount);
    return 0;
}

void svc_handle_exit(struct svc_manager *mgr, int pid, int status) {
    for (int i = 0; i < mgr->count; i++) {
        struct svc_runtime *rt = &mgr->runtime[i];
        if (rt->pid == pid) {
            struct svc_config *cfg = &mgr->configs[i];
            printf("%s exited (status=%d)\n", cfg->name, status);

            rt->state          = SVC_STATE_STOPPED;
            rt->pid            = -1;
            rt->start_ticks    = 0;
            rt->reported_ready = false;
            rt->mounted        = false;
            rt->ready          = false;

            if (cfg->respawn) {
                printf("Respawning %s...\n", cfg->name);
                rt->state = SVC_STATE_PENDING;
            }

            return;
        }
    }
}
