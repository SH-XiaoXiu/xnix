#include "svc_internal.h"

#include <stdio.h>
#include <string.h>
#include <xnix/ipc.h>
#include <xnix/ulog.h>

void svc_handle_ready_notification(struct svc_manager *mgr, struct ipc_message *msg) {
    if (msg->regs.data[0] != SVC_MSG_READY) {
        return;
    }

    uint32_t pid = msg->regs.data[1];
    char     name[16];
    memset(name, 0, sizeof(name));

    if (pid != 0) {
        memcpy(name, &msg->regs.data[2], sizeof(name));
        name[sizeof(name) - 1] = '\0';
    } else if (msg->buffer.data && msg->buffer.size >= sizeof(struct svc_ready_msg)) {
        struct svc_ready_msg *ready = (struct svc_ready_msg *)msg->buffer.data;
        if (ready->magic != SVC_MSG_READY) {
            return;
        }
        pid = ready->pid;
        memcpy(name, ready->name, sizeof(name));
        name[sizeof(name) - 1] = '\0';
    } else {
        return;
    }

    int idx = svc_find_by_name(mgr, name);

    if (idx >= 0 && mgr->runtime[idx].state == SVC_STATE_RUNNING &&
        mgr->runtime[idx].pid == (int)pid) {
        mgr->runtime[idx].reported_ready = true;
        if (mgr->configs[idx].mount[0] == '\0') {
            mgr->runtime[idx].ready = true;
        }
        ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[INIT] ", "Service '%s' reported ready\n", name);
    }
}
