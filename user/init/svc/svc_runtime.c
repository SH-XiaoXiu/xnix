#include "early_console.h"
#include "ramfs.h"
#include "ramfsd_service.h"
#include "svc_internal.h"

#include <d/protocol/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/process.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

#include "bootstrap/bootstrap.h"

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
        printf("Failed to mount %s: %d\n", cfg->mount, ret);
    }
    return ret;
}

/**
 * 解析 args 字符串, 填充 exec_args->argv 和 argc
 */
static void build_argv(struct abi_exec_args *exec_args, char *args) {
    exec_args->argc = 0;
    if (args[0] == '\0') {
        return;
    }

    char *p      = args;
    int   in_arg = 0;
    char *start  = p;

    for (;; p++) {
        if (*p == ' ' || *p == '\0') {
            if (in_arg && exec_args->argc < ABI_EXEC_MAX_ARGS) {
                size_t len = p - start;
                if (len >= ABI_EXEC_MAX_ARG_LEN) {
                    len = ABI_EXEC_MAX_ARG_LEN - 1;
                }
                memcpy(exec_args->argv[exec_args->argc], start, len);
                exec_args->argv[exec_args->argc][len] = '\0';
                exec_args->argc++;
                in_arg = 0;
            }
            if (*p == '\0') {
                break;
            }
        } else if (!in_arg) {
            start  = p;
            in_arg = 1;
        }
    }
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
 * 向 handle 数组追加标准 stdio handles(stdin/stdout/stderr → tty)
 */
static int inject_stdio_handles(struct spawn_handle *handles, int count, int max,
                                uint32_t tty_handle) {
    if (tty_handle == HANDLE_INVALID || count >= max) {
        return count;
    }
    const char *names[] = {HANDLE_STDIO_STDIN, HANDLE_STDIO_STDOUT, HANDLE_STDIO_STDERR};
    for (int i = 0; i < 3 && count < max; i++) {
        handles[count].src = tty_handle;
        snprintf(handles[count].name, sizeof(handles[count].name), "%s", names[i]);
        count++;
    }
    return count;
}

/**
 * 从 ramfs 加载 ELF 并使用 bootstrap_exec 启动
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

    /* 准备 handles */
    struct spawn_handle handles[ABI_EXEC_MAX_HANDLES];
    int                 handle_count = 0;

    for (int i = 0; i < cfg->handle_count && i < ABI_EXEC_MAX_HANDLES; i++) {
        handles[handle_count].src = cfg->handles[i].src_handle;
        snprintf(handles[handle_count].name, sizeof(handles[handle_count].name), "%s",
                 cfg->handles[i].name);
        handle_count++;
    }

    /* 注入 init_notify(优先于 stdio 确保 ready 通知可达) */
    if (mgr->init_notify_ep != HANDLE_INVALID && handle_count < ABI_EXEC_MAX_HANDLES) {
        handles[handle_count].src = mgr->init_notify_ep;
        snprintf(handles[handle_count].name, sizeof(handles[handle_count].name), "%s",
                 "init_notify");
        handle_count++;
    }

    /* 注入标准 stdio handles */
    uint32_t tty_h = find_tty_handle(cfg);
    handle_count   = inject_stdio_handles(handles, handle_count, ABI_EXEC_MAX_HANDLES, tty_h);

    int pid = bootstrap_exec(elf_data, (size_t)nread, cfg->name, NULL, handles, handle_count,
                             cfg->profile[0] ? cfg->profile : NULL);

    free(elf_data);
    return pid;
}

/**
 * 通过 sys_exec (VFS) 启动
 */
static int start_via_exec(struct svc_manager *mgr, struct svc_config *cfg) {
    struct abi_exec_args exec_args;
    memset(&exec_args, 0, sizeof(exec_args));

    size_t path_len = strlen(cfg->path);
    if (path_len >= ABI_EXEC_PATH_MAX) {
        path_len = ABI_EXEC_PATH_MAX - 1;
    }
    memcpy(exec_args.path, cfg->path, path_len);
    exec_args.path[path_len] = '\0';

    if (cfg->profile[0] != '\0') {
        size_t profile_len = strlen(cfg->profile);
        if (profile_len >= ABI_SPAWN_PROFILE_LEN) {
            profile_len = ABI_SPAWN_PROFILE_LEN - 1;
        }
        memcpy(exec_args.profile_name, cfg->profile, profile_len);
        exec_args.profile_name[profile_len] = '\0';
    }

    build_argv(&exec_args, cfg->args);
    exec_args.flags = 0;

    int hcount = 0;
    for (int i = 0; i < cfg->handle_count && i < ABI_EXEC_MAX_HANDLES; i++) {
        exec_args.handles[hcount].src = cfg->handles[i].src_handle;
        snprintf(exec_args.handles[hcount].name, sizeof(exec_args.handles[hcount].name), "%s",
                 cfg->handles[i].name);
        hcount++;
    }

    /* 注入 init_notify(优先于 stdio 确保 ready 通知可达) */
    if (mgr->init_notify_ep != HANDLE_INVALID && hcount < ABI_EXEC_MAX_HANDLES) {
        exec_args.handles[hcount].src = mgr->init_notify_ep;
        snprintf(exec_args.handles[hcount].name, sizeof(exec_args.handles[hcount].name), "%s",
                 "init_notify");
        hcount++;
    }

    /* 注入标准 stdio handles */
    uint32_t tty_h = find_tty_handle(cfg);
    hcount         = inject_stdio_handles(exec_args.handles, hcount, ABI_EXEC_MAX_HANDLES, tty_h);

    exec_args.handle_count = (uint32_t)hcount;

    return sys_exec(&exec_args);
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
            snprintf(buf, sizeof(buf), "[INIT] ERROR: failed to start %s (err %d)\n", cfg->name,
                     pid);
            early_puts(buf);
        } else {
            printf("Failed to start %s: %d\n", cfg->name, pid);
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
        svc_suppress_diagnostics();
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
