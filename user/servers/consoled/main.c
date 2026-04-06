#include <xnix/abi/io.h>
#include <xnix/protocol/console.h>
#include <xnix/protocol/tty.h>
#include <xnix/protocol/ws.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

static handle_t g_console_ep = HANDLE_INVALID;
static handle_t g_tty0_ep = HANDLE_INVALID;
static handle_t g_tty5_ep = HANDLE_INVALID;
static handle_t g_tty6_ep = HANDLE_INVALID;
static handle_t g_ws_ep = HANDLE_INVALID;

struct console_session {
    enum console_session_type type;
    int                       id;
    handle_t                  control_ep;
};

static struct console_session g_active_session = {
    .type = CONSOLE_SESSION_BOOT,
    .id = 0,
    .control_ep = HANDLE_INVALID,
};

static handle_t tty_handle_by_id(int tty_id) {
    switch (tty_id) {
    case 0:
        return g_tty0_ep;
    case 5:
        return g_tty5_ep;
    case 6:
        return g_tty6_ep;
    default:
        return HANDLE_INVALID;
    }
}

static struct console_session tty_session_by_id(int tty_id) {
    struct console_session session = {
        .type = CONSOLE_SESSION_TTY,
        .id = tty_id,
        .control_ep = tty_handle_by_id(tty_id),
    };
    return session;
}

static int tty_set_active(handle_t tty_ep, int tty_id) {
    struct ipc_message msg = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = IO_IOCTL;
    msg.regs.data[1] = 0;
    msg.regs.data[2] = TTY_IOCTL_SET_ACTIVE_TTY;
    msg.regs.data[3] = (uint32_t)tty_id;

    int ret = sys_ipc_call(tty_ep, &msg, &reply, 1000);
    if (ret < 0)
        return ret;
    return (int32_t)reply.regs.data[0];
}

static int tty_release_active(void) {
    struct ipc_message msg = {0};
    struct ipc_message reply = {0};

    msg.regs.data[0] = IO_IOCTL;
    msg.regs.data[1] = 0;
    msg.regs.data[2] = TTY_IOCTL_SET_ACTIVE_TTY;
    msg.regs.data[3] = UINT32_MAX;

    int ret = sys_ipc_call(g_tty0_ep, &msg, &reply, 1000);
    if (ret < 0)
        return ret;
    return (int32_t)reply.regs.data[0];
}

static int ws_set_active(int active) {
    if (g_ws_ep == HANDLE_INVALID)
        return -1;

    struct ipc_message msg = {0};
    struct ipc_message reply = {0};
    msg.regs.data[0] = WS_OP_SET_ACTIVE;
    msg.regs.data[1] = (uint32_t)(active ? 1 : 0);

    int ret = sys_ipc_call(g_ws_ep, &msg, &reply, 1000);
    if (ret < 0)
        return ret;
    return (int32_t)reply.regs.data[0];
}

static int console_activate_tty(int tty_id) {
    struct console_session session = tty_session_by_id(tty_id);
    if (session.control_ep == HANDLE_INVALID)
        return -1;

    if (g_active_session.type == CONSOLE_SESSION_GUI) {
        int ws_ret = ws_set_active(0);
        if (ws_ret < 0)
            return ws_ret;
    }

    int ret = tty_set_active(session.control_ep, tty_id);
    if (ret < 0)
        return ret;

    g_active_session = session;
    return 0;
}

int main(void) {
    env_set_name("consoled");

    g_console_ep = env_get_handle("console_ep");
    g_tty0_ep = env_get_handle("tty0");
    g_tty5_ep = env_get_handle("tty5");
    g_tty6_ep = env_get_handle("tty6");
    g_ws_ep = env_get_handle("ws_ep");

    if (g_console_ep == HANDLE_INVALID || g_tty0_ep == HANDLE_INVALID) {
        ulog_errf("[consoled] missing console_ep or tty0\n");
        return 1;
    }

    svc_notify_ready("consoled");

    while (1) {
        struct ipc_message msg = {0};
        if (sys_ipc_receive(g_console_ep, &msg, 0) < 0)
            continue;

        switch (msg.regs.data[0]) {
        case CONSOLE_OP_SET_ACTIVE_TTY: {
            int tty_id = (int)msg.regs.data[1];
            int ret = console_activate_tty(tty_id);
            if (ret < 0) {
                msg.regs.data[0] = (uint32_t)ret;
                break;
            }
            msg.regs.data[0] = 0;
            break;
        }
        case CONSOLE_OP_GET_ACTIVE_TTY:
            msg.regs.data[0] =
                (uint32_t)(g_active_session.type == CONSOLE_SESSION_TTY ? g_active_session.id : -1);
            break;
        case CONSOLE_OP_GET_ACTIVE_SESSION:
            msg.regs.data[0] = (uint32_t)g_active_session.type;
            msg.regs.data[1] = (uint32_t)g_active_session.id;
            break;
        case CONSOLE_OP_SET_ACTIVE_GUI:
            if (g_ws_ep == HANDLE_INVALID) {
                msg.regs.data[0] = (uint32_t)-1;
                break;
            }
            if (tty_release_active() < 0) {
                msg.regs.data[0] = (uint32_t)-1;
                break;
            }
            if (ws_set_active(1) < 0) {
                msg.regs.data[0] = (uint32_t)-1;
                break;
            }
            g_active_session.type = CONSOLE_SESSION_GUI;
            g_active_session.id = 0;
            g_active_session.control_ep = g_ws_ep;
            msg.regs.data[0] = 0;
            break;
        case CONSOLE_OP_HANDOFF_BOOT: {
            int tty_id = (int)msg.regs.data[1];
            if (g_active_session.type != CONSOLE_SESSION_BOOT) {
                msg.regs.data[0] = (uint32_t)-1;
                break;
            }
            int ret = console_activate_tty(tty_id);
            if (ret < 0) {
                msg.regs.data[0] = (uint32_t)ret;
                break;
            }
            msg.regs.data[0] = 0;
            break;
        }
        default:
            msg.regs.data[0] = (uint32_t)-1;
            break;
        }

        sys_ipc_reply(&msg);
    }
}
