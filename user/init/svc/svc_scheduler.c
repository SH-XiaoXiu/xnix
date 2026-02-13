#include "svc_internal.h"

#include <stdio.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/ulog.h>

static uint32_t g_ticks                = 0;
static uint32_t g_last_diag_ticks      = 0;
static bool     g_suppress_diagnostics = false;

#define SVC_READY_TIMEOUT_MS 5000
#define SVC_DIAG_INTERVAL_MS 2000

/**
 * 检查是否有其他服务依赖此服务的就绪状态
 */
static bool svc_is_ready_depended(struct svc_manager *mgr, int idx) {
    const char *name = mgr->configs[idx].name;
    for (int i = 0; i < mgr->count; i++) {
        if (i == idx) {
            continue;
        }
        struct svc_config *other = &mgr->configs[i];
        for (int j = 0; j < other->ready_count; j++) {
            if (strcmp(other->ready[j], name) == 0) {
                return true;
            }
        }
    }
    return false;
}

static void svc_check_ready_timeouts(struct svc_manager *mgr) {
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config  *cfg = &mgr->configs[i];
        struct svc_runtime *rt  = &mgr->runtime[i];

        if (rt->state != SVC_STATE_RUNNING || rt->ready) {
            continue;
        }

        uint32_t elapsed = g_ticks - rt->start_ticks;
        if (elapsed < SVC_READY_TIMEOUT_MS) {
            continue;
        }

        /* 无其他服务依赖其就绪状态时,静默标记为就绪 */
        if (!svc_is_ready_depended(mgr, i)) {
            rt->ready = true;
            continue;
        }

        ulog_tagf(stdout, TERM_COLOR_LIGHT_BROWN, "[INIT] ",
                  "Timeout: %s not ready (pid=%d, elapsed=%u)\n", cfg->name, rt->pid, elapsed);
        rt->state = SVC_STATE_FAILED;
    }
}

static void svc_propagate_failed_requires(struct svc_manager *mgr) {
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config  *cfg = &mgr->configs[i];
        struct svc_runtime *rt  = &mgr->runtime[i];

        if (rt->state != SVC_STATE_PENDING) {
            continue;
        }

        for (int j = 0; j < cfg->ready_count; j++) {
            int dep = svc_find_by_name(mgr, cfg->ready[j]);
            if (dep < 0) {
                continue;
            }

            if (mgr->runtime[dep].state == SVC_STATE_FAILED) {
                ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[INIT] ", "Failed: %s requires %s\n",
                          cfg->name, cfg->ready[j]);
                rt->state = SVC_STATE_FAILED;
                break;
            }
        }
    }
}

static void svc_dump_waiting(struct svc_manager *mgr) {
    if (g_suppress_diagnostics) {
        return;
    }

    /* 先检查是否有需要报告的服务 */
    bool has_waiting = false;
    for (int i = 0; i < mgr->count; i++) {
        struct svc_runtime *rt = &mgr->runtime[i];
        if (rt->state == SVC_STATE_PENDING || rt->state == SVC_STATE_WAITING ||
            (rt->state == SVC_STATE_RUNNING && !rt->ready) || rt->state == SVC_STATE_FAILED) {
            has_waiting = true;
            break;
        }
    }

    if (!has_waiting) {
        return;
    }

    ulog_tagf(stdout, TERM_COLOR_LIGHT_CYAN, "[INIT] ", "Services waiting:\n");
    for (int i = 0; i < mgr->count; i++) {
        struct svc_config  *cfg = &mgr->configs[i];
        struct svc_runtime *rt  = &mgr->runtime[i];

        if (rt->state == SVC_STATE_PENDING) {
            const char *reason = "conditions not met";
            const char *dep    = "";

            for (int j = 0; j < cfg->ready_count; j++) {
                int d = svc_find_by_name(mgr, cfg->ready[j]);
                if (d >= 0 && !mgr->runtime[d].ready) {
                    reason = "waiting ready";
                    dep    = cfg->ready[j];
                    break;
                }
            }
            if (dep[0] == '\0') {
                for (int j = 0; j < cfg->after_count; j++) {
                    int d = svc_find_by_name(mgr, cfg->after[j]);
                    if (d >= 0 && mgr->runtime[d].state < SVC_STATE_STARTING) {
                        reason = "waiting after";
                        dep    = cfg->after[j];
                        break;
                    }
                }
            }

            printf("  %s: PENDING (%s %s)\n", cfg->name, reason, dep);
        } else if (rt->state == SVC_STATE_WAITING) {
            uint32_t elapsed = g_ticks - rt->delay_start;
            uint32_t total   = cfg->delay_ms;
            printf("  %s: WAITING (%u/%u)\n", cfg->name, elapsed, total);
        } else if (rt->state == SVC_STATE_RUNNING && !rt->ready) {
            uint32_t    elapsed = g_ticks - rt->start_ticks;
            const char *what    = rt->reported_ready ? "mount" : "ready";
            printf("  %s: RUNNING (waiting %s, %u)\n", cfg->name, what, elapsed);
        } else if (rt->state == SVC_STATE_FAILED) {
            printf("  %s: FAILED\n", cfg->name);
        }
    }
}

uint32_t svc_get_ticks(void) {
    return g_ticks;
}

bool svc_can_start(struct svc_manager *mgr, int idx) {
    struct svc_config *cfg = &mgr->configs[idx];

    for (int i = 0; i < cfg->after_count; i++) {
        int dep = svc_find_by_name(mgr, cfg->after[i]);
        if (dep < 0) {
            continue;
        }
        if (mgr->runtime[dep].state < SVC_STATE_STARTING) {
            return false;
        }
    }

    for (int i = 0; i < cfg->ready_count; i++) {
        int dep = svc_find_by_name(mgr, cfg->ready[i]);
        if (dep < 0) {
            continue;
        }
        if (!mgr->runtime[dep].ready) {
            return false;
        }
    }

    if (cfg->wait_path[0]) {
        struct vfs_stat st;
        if (vfs_stat(cfg->wait_path, &st) < 0) {
            return false;
        }
    }

    return true;
}

void svc_tick(struct svc_manager *mgr) {
    g_ticks += 50;

    for (int i = 0; i < mgr->count; i++) {
        svc_try_mount_service(mgr, i);
    }

    svc_check_ready_timeouts(mgr);
    svc_propagate_failed_requires(mgr);
    if (g_ticks - g_last_diag_ticks >= SVC_DIAG_INTERVAL_MS) {
        svc_dump_waiting(mgr);
        g_last_diag_ticks = g_ticks;
    }

    for (int i = 0; i < mgr->count; i++) {
        struct svc_config  *cfg = &mgr->configs[i];
        struct svc_runtime *rt  = &mgr->runtime[i];

        if (rt->state != SVC_STATE_PENDING) {
            continue;
        }

        if (svc_can_start(mgr, i)) {
            if (cfg->delay_ms > 0) {
                rt->state       = SVC_STATE_WAITING;
                rt->delay_start = g_ticks;
            } else {
                svc_start_service(mgr, i);
            }
        }
    }

    for (int i = 0; i < mgr->count; i++) {
        struct svc_runtime *rt = &mgr->runtime[i];
        if (rt->state == SVC_STATE_WAITING) {
            uint32_t elapsed = g_ticks - rt->delay_start;
            if (elapsed >= mgr->configs[i].delay_ms) {
                svc_start_service(mgr, i);
            }
        }
    }
}

static void svc_process_delays(struct svc_manager *mgr) {
    for (int i = 0; i < mgr->count; i++) {
        struct svc_runtime *rt = &mgr->runtime[i];
        if (rt->state == SVC_STATE_WAITING) {
            uint32_t elapsed = g_ticks - rt->delay_start;
            if (elapsed >= mgr->configs[i].delay_ms) {
                svc_start_service(mgr, i);
            }
        }
    }
}

void svc_tick_parallel(struct svc_manager *mgr) {
    g_ticks += 50;

    svc_process_delays(mgr);

    for (int i = 0; i < mgr->count; i++) {
        svc_try_mount_service(mgr, i);
    }

    svc_check_ready_timeouts(mgr);
    svc_propagate_failed_requires(mgr);
    if (g_ticks - g_last_diag_ticks >= SVC_DIAG_INTERVAL_MS) {
        svc_dump_waiting(mgr);
        g_last_diag_ticks = g_ticks;
    }

    for (int n = 0; n < mgr->count; n++) {
        int idx = mgr->topo_order[n];

        struct svc_runtime *rt = &mgr->runtime[idx];
        if (rt->state != SVC_STATE_PENDING) {
            continue;
        }

        if (!svc_can_start_advanced(mgr, idx)) {
            continue;
        }

        if (mgr->configs[idx].delay_ms > 0) {
            rt->state       = SVC_STATE_WAITING;
            rt->delay_start = g_ticks;
        } else {
            svc_start_service(mgr, idx);
        }
    }
}

void svc_suppress_diagnostics(void) {
    g_suppress_diagnostics = true;
}

bool svc_is_quiet(void) {
    return g_suppress_diagnostics;
}
