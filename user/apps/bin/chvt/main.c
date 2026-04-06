#include <xnix/protocol/console.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    printf("Usage: chvt <tty-id>\n");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        usage();
        return 1;
    }

    char *end = NULL;
    long tty_id = strtol(argv[1], &end, 10);
    if (!end || *end != '\0' || tty_id < 0) {
        usage();
        return 1;
    }

    handle_t console_ep = sys_handle_find("console_ep");
    if (console_ep == HANDLE_INVALID) {
        printf("chvt: console_ep not found\n");
        return 1;
    }

    struct ipc_message msg = {0};
    struct ipc_message reply = {0};
    msg.regs.data[0] = CONSOLE_OP_SET_ACTIVE_TTY;
    msg.regs.data[1] = (uint32_t)tty_id;

    if (sys_ipc_call(console_ep, &msg, &reply, 1000) < 0 ||
        (int32_t)reply.regs.data[0] < 0) {
        printf("chvt: tty%d is not a display VT or switch failed\n", (int)tty_id);
        return 1;
    }
    return 0;
}
