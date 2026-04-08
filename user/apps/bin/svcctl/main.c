/**
 * @file main.c
 * @brief svcctl - 服务管理命令行工具
 *
 * 通过 init_admin IPC endpoint 控制系统服务.
 *
 * 用法:
 *   svcctl list                列出所有服务及状态
 *   svcctl status <name>       查看服务详细状态
 *   svcctl stop <name>         停止服务
 *   svcctl start <name>        启动服务
 *   svcctl restart <name>      重启服务
 */

#include <stdio.h>
#include <string.h>
#include <xnix/abi/handle.h>
#include <xnix/ipc.h>
#include <xnix/protocol/svc.h>
#include <xnix/syscall.h>

static const char *state_names[] = {
    "PENDING", "WAITING", "STARTING", "RUNNING", "STOPPED", "FAILED",
};

static const char *state_str(uint32_t s) {
    if (s < sizeof(state_names) / sizeof(state_names[0])) {
        return state_names[s];
    }
    return "UNKNOWN";
}

static const char *reply_str(uint32_t code) {
    switch (code) {
    case SVC_REPLY_OK:        return NULL;
    case SVC_REPLY_NOT_FOUND: return "service not found";
    case SVC_REPLY_ALREADY:   return "already in that state";
    case SVC_REPLY_ERROR:     return "error";
    default:                  return "unknown error";
    }
}

static handle_t get_admin_ep(void) {
    handle_t ep = sys_handle_find("init_notify");
    if (ep == HANDLE_INVALID) {
        printf("svcctl: cannot find init_notify endpoint\n");
    }
    return ep;
}

static void set_name(struct ipc_message *msg, const char *name) {
    size_t len = strlen(name);
    if (len >= 16) {
        len = 16 - 1;
    }
    memcpy(&msg->regs.data[1], name, len);
    /* zero-fill remaining bytes */
    if (len < 16) {
        memset((char *)&msg->regs.data[1] + len, 0, 16 - len);
    }
}

static int do_simple_cmd(uint32_t opcode, const char *name) {
    handle_t ep = get_admin_ep();
    if (ep == HANDLE_INVALID) return 1;

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    msg.regs.data[0] = opcode;
    set_name(&msg, name);

    int ret = sys_ipc_call((uint32_t)ep, &msg, &reply, 5000);
    if (ret != 0) {
        printf("svcctl: ipc call failed\n");
        return 1;
    }

    const char *err = reply_str(reply.regs.data[0]);
    if (err) {
        printf("svcctl: %s: %s\n", name, err);
        return 1;
    }

    return 0;
}

static int cmd_stop(const char *name) {
    int ret = do_simple_cmd(SVC_MSG_STOP, name);
    if (ret == 0) printf("Stopped %s\n", name);
    return ret;
}

static int cmd_start(const char *name) {
    int ret = do_simple_cmd(SVC_MSG_START, name);
    if (ret == 0) printf("Started %s\n", name);
    return ret;
}

static int cmd_restart(const char *name) {
    int ret = do_simple_cmd(SVC_MSG_RESTART, name);
    if (ret == 0) printf("Restarted %s\n", name);
    return ret;
}

static int cmd_status(const char *name) {
    handle_t ep = get_admin_ep();
    if (ep == HANDLE_INVALID) return 1;

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    msg.regs.data[0] = SVC_MSG_STATUS;
    set_name(&msg, name);

    int ret = sys_ipc_call((uint32_t)ep, &msg, &reply, 5000);
    if (ret != 0) {
        printf("svcctl: ipc call failed\n");
        return 1;
    }

    const char *err = reply_str(reply.regs.data[0]);
    if (err) {
        printf("svcctl: %s: %s\n", name, err);
        return 1;
    }

    uint32_t state  = reply.regs.data[1];
    int      pid    = (int)reply.regs.data[2];
    int      ready  = (int)reply.regs.data[3];
    uint32_t uptime = reply.regs.data[4];

    printf("%-16s %s\n", "Service:", name);
    printf("%-16s %s\n", "State:", state_str(state));
    printf("%-16s %d\n", "PID:", pid);
    printf("%-16s %s\n", "Ready:", ready ? "yes" : "no");
    if (uptime > 0) {
        printf("%-16s %u.%03us\n", "Uptime:", uptime / 1000, uptime % 1000);
    }

    return 0;
}

static int cmd_list(void) {
    handle_t ep = get_admin_ep();
    if (ep == HANDLE_INVALID) return 1;

    struct ipc_message msg   = {0};
    struct ipc_message reply = {0};
    char               buf[1024] = {0};
    msg.regs.data[0]  = SVC_MSG_LIST;
    reply.buffer.data = (uint64_t)(uintptr_t)buf;
    reply.buffer.size = sizeof(buf) - 1;

    int ret = sys_ipc_call((uint32_t)ep, &msg, &reply, 5000);
    if (ret != 0) {
        printf("svcctl: ipc call failed\n");
        return 1;
    }

    if (reply.regs.data[0] != SVC_REPLY_OK) {
        printf("svcctl: list failed\n");
        return 1;
    }

    uint32_t count = reply.regs.data[1];
    printf("%-16s %-10s %-6s %s\n", "SERVICE", "STATE", "PID", "READY");

    /* 解析 buffer 中的行: "name state pid ready\n" */
    char *line = buf;
    for (uint32_t i = 0; i < count; i++) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char     svc_name[16] = {0};
        char     svc_state[16]          = {0};
        int      svc_pid                = -1;
        int      svc_ready              = 0;

        /* 手动解析 "name state pid ready" */
        char *p = line;
        char *tok;

        /* name */
        tok = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
        strncpy(svc_name, tok, sizeof(svc_name) - 1);

        /* state */
        tok = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
        strncpy(svc_state, tok, sizeof(svc_state) - 1);

        /* pid */
        tok = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
        svc_pid = 0;
        {
            const char *s = tok;
            int neg = 0;
            if (*s == '-') { neg = 1; s++; }
            while (*s >= '0' && *s <= '9') {
                svc_pid = svc_pid * 10 + (*s - '0');
                s++;
            }
            if (neg) svc_pid = -svc_pid;
        }

        /* ready */
        tok = p;
        if (*tok == '1') svc_ready = 1;

        printf("%-16s %-10s %-6d %s\n", svc_name, svc_state, svc_pid,
               svc_ready ? "yes" : "no");

        if (!nl) break;
        line = nl + 1;
    }

    return 0;
}

static void usage(void) {
    printf("Usage: svcctl <command> [name]\n");
    printf("Commands:\n");
    printf("  list              List all services\n");
    printf("  status <name>     Show service status\n");
    printf("  stop <name>       Stop a service\n");
    printf("  start <name>      Start a service\n");
    printf("  restart <name>    Restart a service\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "list") == 0) {
        return cmd_list();
    }

    if (strcmp(cmd, "status") == 0 || strcmp(cmd, "stop") == 0 ||
        strcmp(cmd, "start") == 0 || strcmp(cmd, "restart") == 0) {
        if (argc < 3) {
            printf("svcctl %s: missing service name\n", cmd);
            return 1;
        }
        const char *name = argv[2];

        if (strcmp(cmd, "status") == 0)  return cmd_status(name);
        if (strcmp(cmd, "stop") == 0)    return cmd_stop(name);
        if (strcmp(cmd, "start") == 0)   return cmd_start(name);
        if (strcmp(cmd, "restart") == 0) return cmd_restart(name);
    }

    printf("svcctl: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
