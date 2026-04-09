/**
 * @file main.c
 * @brief userd: 用户管理服务
 *
 * 职责:
 *   - 用户认证 (LOGIN)
 *   - 会话管理 (WHOAMI, LOGOUT, VALIDATE)
 *   - 权限提升 (SUDO)
 *
 * 架构:
 *   user_ep: 接收 LOGIN 请求 (全局)
 *   session endpoint: 接收 WHOAMI/LOGOUT/VALIDATE/SUDO (per-session)
 *   userd 用 wait_any 同时监听 user_ep + 所有 session endpoint
 */

#include "passwd.h"
#include "session.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <vfs_client.h>
#include <xnix/abi/ipc.h>
#include <xnix/abi/process.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/proc.h>
#include <xnix/protocol/user.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

#define RECV_BUF_SIZE 8192

static handle_t g_user_ep;
static char     g_recv_buf[RECV_BUF_SIZE];
static char     g_reply_buf[RECV_BUF_SIZE];

/*
 * LOGIN 处理
 */
static void handle_login(struct ipc_message *msg) {
    struct user_login_req *req = (struct user_login_req *)(uintptr_t)msg->buffer.data;

    if (!req || msg->buffer.size < sizeof(*req)) {
        msg->regs.data[0] = USER_OP_LOGIN_REPLY;
        msg->regs.data[1] = (uint32_t)(-EINVAL);
        msg->buffer.data  = 0;
        msg->buffer.size  = 0;
        return;
    }

    /* 确保字符串终止 */
    char username[USER_NAME_MAX] = {0};
    char password[USER_PASS_MAX] = {0};
    strncpy(username, req->username, USER_NAME_MAX - 1);
    strncpy(password, req->password, USER_PASS_MAX - 1);

    printf("[userd] login attempt: user='%s' from pid=%u\n",
           username, msg->sender_pid);

    /* 查找用户 */
    struct passwd_entry *ent = passwd_lookup(username);
    if (!ent) {
        printf("[userd] login failed: user '%s' not found\n", username);
        msg->regs.data[0] = USER_OP_LOGIN_REPLY;
        msg->regs.data[1] = (uint32_t)(-ENOENT);
        msg->buffer.data  = 0;
        msg->buffer.size  = 0;
        return;
    }

    /* 验证密码 */
    if (!passwd_verify(ent, password)) {
        printf("[userd] login failed: wrong password for '%s'\n", username);
        msg->regs.data[0] = USER_OP_LOGIN_REPLY;
        msg->regs.data[1] = (uint32_t)(-EACCES);
        msg->buffer.data  = 0;
        msg->buffer.size  = 0;
        return;
    }

    /* 创建会话 */
    int sid = session_create(ent->uid, ent->gid, ent->name, ent->home, ent->shell);
    if (sid < 0) {
        msg->regs.data[0] = USER_OP_LOGIN_REPLY;
        msg->regs.data[1] = (uint32_t)(-ENOMEM);
        msg->buffer.data  = 0;
        msg->buffer.size  = 0;
        return;
    }

    struct session *sess = session_get(sid);

    /* 回复: session endpoint handle + user_info */
    struct user_info *info = (struct user_info *)g_reply_buf;
    session_fill_info(sid, info);

    msg->regs.data[0]       = USER_OP_LOGIN_REPLY;
    msg->regs.data[1]       = 0; /* 成功 */
    msg->handles.handles[0] = sess->ep;
    msg->handles.count      = 1;
    msg->buffer.data        = (uint64_t)(uintptr_t)info;
    msg->buffer.size        = sizeof(*info);

    printf("[userd] login success: user='%s' uid=%u session=%d\n",
           ent->name, ent->uid, sid);
}

/*
 * WHOAMI / VALIDATE 处理
 */
static void handle_whoami(int session_idx, struct ipc_message *msg, uint32_t reply_op) {
    struct user_info *info = (struct user_info *)g_reply_buf;
    session_fill_info(session_idx, info);

    msg->regs.data[0] = reply_op;
    msg->regs.data[1] = 0;
    msg->buffer.data  = (uint64_t)(uintptr_t)info;
    msg->buffer.size  = sizeof(*info);
    msg->handles.count = 0;
}

/*
 * LOGOUT 处理
 */
static void handle_logout(int session_idx, struct ipc_message *msg) {
    session_destroy(session_idx);

    msg->regs.data[0]  = USER_OP_LOGOUT_REPLY;
    msg->regs.data[1]  = 0;
    msg->buffer.data   = 0;
    msg->buffer.size   = 0;
    msg->handles.count = 0;
}

/*
 * SUDO 处理 (发到 session endpoint)
 *
 * 请求:
 *   regs[0] = USER_OP_SUDO
 *   regs[1] = target_uid (默认 0)
 *   buffer = struct user_sudo_req (password + exec_args)
 *
 * userd 验证身份后以目标用户权限 spawn 命令.
 */
static void handle_sudo(int session_idx, struct ipc_message *msg) {
    struct session *sess = session_get(session_idx);
    if (!sess) {
        msg->regs.data[0] = USER_OP_SUDO_REPLY;
        msg->regs.data[1] = (uint32_t)(-EINVAL);
        msg->buffer.data  = 0;
        msg->buffer.size  = 0;
        return;
    }

    struct user_sudo_req *req = (struct user_sudo_req *)(uintptr_t)msg->buffer.data;
    if (!req || msg->buffer.size < sizeof(struct user_sudo_req)) {
        msg->regs.data[0] = USER_OP_SUDO_REPLY;
        msg->regs.data[1] = (uint32_t)(-EINVAL);
        msg->buffer.data  = 0;
        msg->buffer.size  = 0;
        return;
    }

    uint32_t target_uid = msg->regs.data[1];

    /* 验证调用者密码 (re-auth) */
    char password[USER_PASS_MAX] = {0};
    strncpy(password, req->password, USER_PASS_MAX - 1);

    struct passwd_entry *caller = passwd_lookup(sess->name);
    if (!caller || !passwd_verify(caller, password)) {
        printf("[userd] sudo: auth failed for user '%s'\n", sess->name);
        msg->regs.data[0] = USER_OP_SUDO_REPLY;
        msg->regs.data[1] = (uint32_t)(-EACCES);
        msg->buffer.data  = 0;
        msg->buffer.size  = 0;
        return;
    }

    /* 查找目标用户 */
    struct passwd_entry *target = passwd_lookup_uid(target_uid);
    if (!target) {
        printf("[userd] sudo: target uid %u not found\n", target_uid);
        msg->regs.data[0] = USER_OP_SUDO_REPLY;
        msg->regs.data[1] = (uint32_t)(-ENOENT);
        msg->buffer.data  = 0;
        msg->buffer.size  = 0;
        return;
    }

    printf("[userd] sudo: user '%s' -> uid %u, cmd='%s'\n",
           sess->name, target_uid, req->exec_args.path);

    /* 为目标用户创建临时 session */
    int target_sid = session_create(target->uid, target->gid,
                                    target->name, target->home, target->shell);

    /* 以目标用户身份 spawn 命令 */
    struct abi_exec_args *exec = &req->exec_args;
    exec->flags |= ABI_EXEC_INHERIT_STDIO;

    /* 如果目标会话创建成功, 注入 session handle */
    if (target_sid >= 0) {
        struct session *ts = session_get(target_sid);
        if (ts && exec->handle_count < ABI_EXEC_MAX_HANDLES) {
            struct spawn_handle *sh = &exec->handles[exec->handle_count++];
            sh->src = ts->ep;
            strncpy(sh->name, "user_session", sizeof(sh->name) - 1);
            sh->rights = HANDLE_RIGHT_ALL;
        }
    }

    /* 注入 vfs_ep 和 user_ep */
    handle_t vfs_ep = env_get_handle("vfs_ep");
    if (vfs_ep != HANDLE_INVALID && exec->handle_count < ABI_EXEC_MAX_HANDLES) {
        struct spawn_handle *sh = &exec->handles[exec->handle_count++];
        sh->src = vfs_ep;
        strncpy(sh->name, "vfs_ep", sizeof(sh->name) - 1);
        sh->rights = HANDLE_RIGHT_ALL;
    }
    if (exec->handle_count < ABI_EXEC_MAX_HANDLES) {
        struct spawn_handle *sh = &exec->handles[exec->handle_count++];
        sh->src = g_user_ep;
        strncpy(sh->name, "user_ep", sizeof(sh->name) - 1);
        sh->rights = HANDLE_RIGHT_ALL;
    }

    int pid = sys_exec(exec);

    printf("[userd] sudo: spawned pid=%d\n", pid);

    /* 如果 spawn 失败, 清理临时 session */
    if (pid < 0 && target_sid >= 0)
        session_destroy(target_sid);

    msg->regs.data[0]  = USER_OP_SUDO_REPLY;
    msg->regs.data[1]  = (uint32_t)pid;
    msg->buffer.data   = 0;
    msg->buffer.size   = 0;
    msg->handles.count = 0;
}

/*
 * 分发 session endpoint 上的请求
 */
static void dispatch_session(int session_idx, struct ipc_message *msg) {
    uint32_t op = msg->regs.data[0];

    switch (op) {
    case USER_OP_WHOAMI:
        handle_whoami(session_idx, msg, USER_OP_WHOAMI_REPLY);
        break;
    case USER_OP_VALIDATE:
        handle_whoami(session_idx, msg, USER_OP_VALIDATE_REPLY);
        break;
    case USER_OP_LOGOUT:
        handle_logout(session_idx, msg);
        break;
    case USER_OP_SUDO:
        handle_sudo(session_idx, msg);
        break;
    default:
        msg->regs.data[0]  = (uint32_t)(-ENOSYS);
        msg->buffer.data   = 0;
        msg->buffer.size   = 0;
        msg->handles.count = 0;
        break;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 获取 user_ep (由 init 注入) */
    g_user_ep = env_require("user_ep");
    if (g_user_ep == HANDLE_INVALID) {
        printf("[userd] FATAL: user_ep not found\n");
        return 1;
    }

    /* 初始化 VFS 客户端 (读取 /etc/passwd) */
    handle_t vfs_ep = env_get_handle("vfs_ep");
    if (vfs_ep != HANDLE_INVALID) {
        vfs_client_init(vfs_ep);
    }

    /* 初始化会话管理 */
    session_init();

    /* 加载用户数据库 */
    if (passwd_load("/etc/passwd") < 0) {
        printf("[userd] WARNING: no /etc/passwd, creating default root user\n");
        /* 如果没有 passwd 文件, 内嵌一个 root 用户 */
    }

    printf("[userd] started, endpoint=%u\n", g_user_ep);

    /* 通知 init 就绪 */
    svc_notify_ready("userd");

    /* 主循环: wait_any 监听 user_ep + 所有 session endpoints */
    while (1) {
        struct abi_ipc_wait_set wait_set;
        memset(&wait_set, 0, sizeof(wait_set));

        /* user_ep 始终在 slot 0 */
        wait_set.handles[0] = g_user_ep;
        int n_sessions = session_fill_wait_set(wait_set.handles, 1);
        wait_set.count = 1 + (uint32_t)n_sessions;

        handle_t ready = sys_ipc_wait_any(&wait_set, 0);
        if (ready == HANDLE_INVALID)
            continue;

        /* 接收消息 */
        struct ipc_message msg;
        memset(&msg, 0, sizeof(msg));
        msg.buffer.data = (uint64_t)(uintptr_t)g_recv_buf;
        msg.buffer.size = RECV_BUF_SIZE;

        if (sys_ipc_receive(ready, &msg, 0) < 0)
            continue;

        if (ready == g_user_ep) {
            /* 全局操作 */
            uint32_t op = msg.regs.data[0];
            if (op == USER_OP_LOGIN) {
                handle_login(&msg);
            } else {
                msg.regs.data[0]  = (uint32_t)(-ENOSYS);
                msg.buffer.data   = 0;
                msg.buffer.size   = 0;
                msg.handles.count = 0;
            }
        } else {
            /* session 操作 */
            int sid = session_find_by_ep(ready);
            if (sid >= 0) {
                dispatch_session(sid, &msg);
            } else {
                msg.regs.data[0]  = (uint32_t)(-EINVAL);
                msg.buffer.data   = 0;
                msg.buffer.size   = 0;
                msg.handles.count = 0;
            }
        }

        /* 回复 (LOGOUT 会销毁 session, 但 reply 用的是 sender_tid) */
        if ((msg.flags & ABI_IPC_FLAG_NOREPLY) == 0) {
            sys_ipc_reply(&msg);
        }
    }

    return 0;
}
