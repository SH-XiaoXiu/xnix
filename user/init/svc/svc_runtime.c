#include "svc_internal.h"

#include <d/protocol/vfs.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/abi/process.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

#include "early_console.h"

static bool probe_fs_ready(uint32_t ep, uint32_t timeout_ms) {
    uint32_t       elapsed        = 0;
    const uint32_t probe_interval = 50;

    while (elapsed < timeout_ms) {
        struct ipc_message msg   = {0};
        struct ipc_message reply = {0};

        msg.regs.data[0]      = UDM_VFS_INFO;
        const char *test_path = ".";
        msg.buffer.data       = (void *)test_path;
        msg.buffer.size       = 2;

        int ret = sys_ipc_call(ep, &msg, &reply, 500);
        if (ret == 0) {
            printf("  Probe succeeded!\n");
            return true;
        }

        if (elapsed < 200) {
            printf("  Probe attempt (elapsed=%ums) failed: ret=%d\n", elapsed, ret);
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

int svc_start_service(struct svc_manager *mgr, int idx) {
    struct svc_config  *cfg = &mgr->configs[idx];
    struct svc_runtime *rt  = &mgr->runtime[idx];

    extern void early_puts(const char *);
    extern bool early_console_is_active(void);
    if (early_console_is_active()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[INIT] starting %s\n", cfg->name);
        early_puts(buf);
    } else {
        printf("Starting %s...\n", cfg->name);
    }

    rt->state = SVC_STATE_STARTING;

    int pid;

    if (cfg->type == SVC_TYPE_PATH) {
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
        } else {
            exec_args.profile_name[0] = '\0';
        }

        exec_args.argc  = 0;
        exec_args.flags = 0;

        exec_args.handle_count = (uint32_t)cfg->handle_count;
        for (int i = 0; i < cfg->handle_count && i < ABI_EXEC_MAX_HANDLES; i++) {
            exec_args.handles[i].src = cfg->handles[i].src_handle;
            snprintf(exec_args.handles[i].name, sizeof(exec_args.handles[i].name), "%s",
                     cfg->handles[i].name);
        }

        if (mgr->init_notify_ep != HANDLE_INVALID &&
            exec_args.handle_count < ABI_EXEC_MAX_HANDLES) {
            int n                    = (int)exec_args.handle_count;
            exec_args.handles[n].src = mgr->init_notify_ep;
            snprintf(exec_args.handles[n].name, sizeof(exec_args.handles[n].name), "%s",
                     "init_notify");
            exec_args.handle_count++;
        }

        pid = sys_exec(&exec_args);
    } else {
        struct spawn_args args;
        memset(&args, 0, sizeof(args));

        size_t name_len = strlen(cfg->name);
        if (name_len >= ABI_SPAWN_NAME_LEN) {
            name_len = ABI_SPAWN_NAME_LEN - 1;
        }
        memcpy(args.name, cfg->name, name_len);
        args.name[name_len] = '\0';

        if (cfg->profile[0] != '\0') {
            size_t profile_len = strlen(cfg->profile);
            if (profile_len >= ABI_SPAWN_PROFILE_LEN) {
                profile_len = ABI_SPAWN_PROFILE_LEN - 1;
            }
            memcpy(args.profile_name, cfg->profile, profile_len);
            args.profile_name[profile_len] = '\0';
        } else {
            args.profile_name[0] = '\0';
        }

        memcpy(args.module_name, cfg->module_name, sizeof(args.module_name));
        args.handle_count = (uint32_t)cfg->handle_count;

        for (int i = 0; i < cfg->handle_count; i++) {
            args.handles[i].src = cfg->handles[i].src_handle;
            snprintf(args.handles[i].name, sizeof(args.handles[i].name), "%s",
                     cfg->handles[i].name);
        }

        if (mgr->init_notify_ep != HANDLE_INVALID && args.handle_count < ABI_SPAWN_MAX_HANDLES) {
            int n               = (int)args.handle_count;
            args.handles[n].src = mgr->init_notify_ep;
            snprintf(args.handles[n].name, sizeof(args.handles[n].name), "%s", "init_notify");
            args.handle_count++;
        }

        pid = sys_spawn(&args);
    }

    if (pid < 0) {
        extern void early_puts(const char *);
        extern bool early_console_is_active(void);
        if (early_console_is_active()) {
            char buf[160];
            snprintf(buf, sizeof(buf), "[INIT] ERROR: failed to start %s\n", cfg->name);
            early_puts(buf);
        } else {
            printf("Failed to start %s: %d\n", cfg->name, pid);
        }
        rt->state = SVC_STATE_FAILED;
        return pid;
    }

    if (early_console_is_active()) {
        extern void early_puts(const char *);
        char        buf[160];
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

    printf("Probing %s readiness (ep=%u for '%s')...\n", cfg->name, cfg->mount_ep, ep_name);
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
