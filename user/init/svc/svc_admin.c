/**
 * @file svc_admin.c
 * @brief init 管理命令 endpoint
 *
 * 提供 init_admin IPC endpoint, 接收并处理 svcctl 发来的
 * STOP / START / RESTART / STATUS / LIST 命令.
 */

#include "svc_internal.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <xnix/abi/handle.h>
#include <xnix/ipc.h>
#include <xnix/protocol/svc.h>
#include <xnix/syscall.h>

static const char *state_str(svc_state_t s) {
    switch (s) {
    case SVC_STATE_PENDING:  return "PENDING";
    case SVC_STATE_WAITING:  return "WAITING";
    case SVC_STATE_STARTING: return "STARTING";
    case SVC_STATE_RUNNING:  return "RUNNING";
    case SVC_STATE_STOPPED:  return "STOPPED";
    case SVC_STATE_FAILED:   return "FAILED";
    default:                 return "UNKNOWN";
    }
}

/**
 * 从 regs data[1..4] 提取服务名 (最多 16 字节)
 */
static void extract_name(const struct ipc_message *msg, char *out) {
    memcpy(out, &msg->regs.data[1], SVC_NAME_MAX);
    out[SVC_NAME_MAX - 1] = '\0';
}

/**
 * 级联重启: kill 并标记 PENDING 所有依赖 target_idx 的服务 (递归)
 */
static void cascade_restart(struct svc_manager *mgr, int target_idx) {
    for (int i = 0; i < mgr->count; i++) {
        struct svc_graph_node *node = &mgr->graph[i];
        for (int j = 0; j < node->dep_count; j++) {
            if (node->deps[j].target_idx == target_idx) {
                struct svc_runtime *rt = &mgr->runtime[i];
                if (rt->state == SVC_STATE_RUNNING || rt->state == SVC_STATE_STARTING) {
                    if (rt->pid > 0) {
                        sys_kill(rt->pid, SIGTERM);
                    }
                    rt->state       = SVC_STATE_PENDING;
                    rt->manual_stop = false;
                    rt->ready       = false;
                    cascade_restart(mgr, i);
                }
                break;
            }
        }
    }
}

/**
 * 返回需要 kill 的 pid (>0), 或 0 表示无需 kill.
 * kill 在 reply 发出之后再执行, 避免阻塞 IPC 回复.
 */
static int handle_stop(struct svc_manager *mgr, const struct ipc_message *msg,
                       struct ipc_message *reply) {
    char name[SVC_NAME_MAX];
    extract_name(msg, name);

    int idx = svc_find_by_name(mgr, name);
    if (idx < 0) {
        reply->regs.data[0] = SVC_REPLY_NOT_FOUND;
        return 0;
    }

    struct svc_runtime *rt = &mgr->runtime[idx];
    if (rt->state == SVC_STATE_STOPPED || rt->state == SVC_STATE_FAILED) {
        reply->regs.data[0] = SVC_REPLY_ALREADY;
        reply->regs.data[1] = (uint32_t)rt->state;
        return 0;
    }

    int kill_pid = rt->pid;
    rt->manual_stop = true;

    reply->regs.data[0] = SVC_REPLY_OK;
    reply->regs.data[1] = (uint32_t)rt->state;
    reply->regs.data[2] = (uint32_t)rt->pid;
    return kill_pid;
}

static void handle_start(struct svc_manager *mgr, const struct ipc_message *msg,
                         struct ipc_message *reply) {
    char name[SVC_NAME_MAX];
    extract_name(msg, name);

    int idx = svc_find_by_name(mgr, name);
    if (idx < 0) {
        reply->regs.data[0] = SVC_REPLY_NOT_FOUND;
        return;
    }

    struct svc_runtime *rt = &mgr->runtime[idx];
    if (rt->state == SVC_STATE_RUNNING) {
        reply->regs.data[0] = SVC_REPLY_ALREADY;
        reply->regs.data[1] = (uint32_t)rt->state;
        reply->regs.data[2] = (uint32_t)rt->pid;
        return;
    }

    rt->manual_stop = false;
    rt->state       = SVC_STATE_PENDING;

    reply->regs.data[0] = SVC_REPLY_OK;
}

/**
 * restart 返回: 高16位=服务索引, 低16位=pid. 用于级联重启.
 * 返回 0 表示未找到/无需操作.
 */
static int handle_restart(struct svc_manager *mgr, const struct ipc_message *msg,
                          struct ipc_message *reply) {
    char name[SVC_NAME_MAX];
    extract_name(msg, name);

    int idx = svc_find_by_name(mgr, name);
    if (idx < 0) {
        reply->regs.data[0] = SVC_REPLY_NOT_FOUND;
        return -1;
    }

    struct svc_runtime *rt = &mgr->runtime[idx];
    int kill_pid = rt->pid;

    rt->manual_stop = false;
    rt->ready       = false;
    rt->state       = SVC_STATE_PENDING;

    reply->regs.data[0] = SVC_REPLY_OK;
    return idx;
}

static void handle_status(struct svc_manager *mgr, const struct ipc_message *msg,
                          struct ipc_message *reply) {
    char name[SVC_NAME_MAX];
    extract_name(msg, name);

    int idx = svc_find_by_name(mgr, name);
    if (idx < 0) {
        reply->regs.data[0] = SVC_REPLY_NOT_FOUND;
        return;
    }

    struct svc_runtime *rt = &mgr->runtime[idx];
    reply->regs.data[0] = SVC_REPLY_OK;
    reply->regs.data[1] = (uint32_t)rt->state;
    reply->regs.data[2] = (uint32_t)rt->pid;
    reply->regs.data[3] = rt->ready ? 1 : 0;

    uint32_t uptime = 0;
    if (rt->state == SVC_STATE_RUNNING && rt->start_ticks > 0) {
        uptime = svc_get_ticks() - rt->start_ticks;
    }
    reply->regs.data[4] = uptime;
}

static void handle_list(struct svc_manager *mgr, const struct ipc_message *msg,
                        struct ipc_message *reply) {
    (void)msg;

    /*
     * 列表格式: 每个服务一条记录, 用 '\n' 分隔
     * 格式: "name state pid ready"
     */
    static char buf[1024];
    int pos = 0;

    for (int i = 0; i < mgr->count && pos < (int)sizeof(buf) - 64; i++) {
        struct svc_config  *cfg = &mgr->configs[i];
        struct svc_runtime *rt  = &mgr->runtime[i];

        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s %s %d %d\n",
                        cfg->name, state_str(rt->state), rt->pid,
                        rt->ready ? 1 : 0);
    }

    reply->regs.data[0] = SVC_REPLY_OK;
    reply->regs.data[1] = (uint32_t)mgr->count;
    reply->buffer.data   = (uint64_t)(uintptr_t)buf;
    reply->buffer.size   = (uint32_t)pos;
}

int svc_admin_init(struct svc_manager *mgr) {
    /* 复用 init_notify endpoint, 不再创建额外 endpoint.
     * 管理命令通过 opcode (>= 0xF010) 与就绪通知 (0xF001) 区分. */
    mgr->init_admin_ep = mgr->init_notify_ep;
    return 0;
}

bool svc_admin_dispatch(struct svc_manager *mgr, struct ipc_message *msg) {
    uint32_t opcode = msg->regs.data[0];

    if (opcode < SVC_MSG_STOP || opcode > SVC_MSG_LIST) {
        return false; /* 不是管理命令, 交给就绪通知处理 */
    }

    struct ipc_message reply = {0};
    int stop_pid    = 0;
    int restart_idx = -1;

    switch (opcode) {
    case SVC_MSG_STOP:    stop_pid = handle_stop(mgr, msg, &reply);       break;
    case SVC_MSG_START:   handle_start(mgr, msg, &reply);                 break;
    case SVC_MSG_RESTART: restart_idx = handle_restart(mgr, msg, &reply); break;
    case SVC_MSG_STATUS:  handle_status(mgr, msg, &reply);                break;
    case SVC_MSG_LIST:    handle_list(mgr, msg, &reply);                  break;
    default:
        reply.regs.data[0] = SVC_REPLY_ERROR;
        break;
    }

    /* 先回复, 再执行 kill / cascade */
    if (msg->sender_tid != 0xFFFFFFFFu) {
        sys_ipc_reply_to(msg->sender_tid, &reply);
    }

    if (stop_pid > 0) {
        sys_kill(stop_pid, SIGTERM);
    }

    if (restart_idx >= 0) {
        struct svc_runtime *rt = &mgr->runtime[restart_idx];
        if (rt->pid > 0) {
            sys_kill(rt->pid, SIGTERM);
        }
        /* 级联重启所有依赖此服务的服务 */
        cascade_restart(mgr, restart_idx);
    }

    return true;
}
