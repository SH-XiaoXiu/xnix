/**
 * @file session.c
 * @brief 用户会话管理
 */

#include "session.h"

#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>

static struct session g_sessions[SESSION_MAX];

void session_init(void) {
    memset(g_sessions, 0, sizeof(g_sessions));
}

int session_create(uint32_t uid, uint32_t gid,
                   const char *name, const char *home, const char *shell) {
    /* 找空闲槽 */
    int idx = -1;
    for (int i = 0; i < SESSION_MAX; i++) {
        if (!g_sessions[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        printf("[userd] session table full\n");
        return -1;
    }

    /* 创建 session endpoint */
    handle_t ep = sys_endpoint_create(NULL);
    if (ep == HANDLE_INVALID) {
        printf("[userd] failed to create session endpoint\n");
        return -1;
    }

    struct session *s = &g_sessions[idx];
    s->active = true;
    s->ep     = ep;
    s->uid    = uid;
    s->gid    = gid;
    strncpy(s->name, name, USER_NAME_MAX - 1);
    strncpy(s->home, home, USER_HOME_MAX - 1);
    strncpy(s->shell, shell, USER_SHELL_MAX - 1);

    printf("[userd] session %d created for user '%s' (uid=%u) ep=%u\n",
           idx, name, uid, ep);
    return idx;
}

int session_find_by_ep(handle_t ep) {
    for (int i = 0; i < SESSION_MAX; i++) {
        if (g_sessions[i].active && g_sessions[i].ep == ep)
            return i;
    }
    return -1;
}

struct session *session_get(int idx) {
    if (idx < 0 || idx >= SESSION_MAX || !g_sessions[idx].active)
        return NULL;
    return &g_sessions[idx];
}

void session_destroy(int idx) {
    if (idx < 0 || idx >= SESSION_MAX)
        return;

    struct session *s = &g_sessions[idx];
    if (!s->active)
        return;

    printf("[userd] session %d destroyed (user '%s')\n", idx, s->name);

    if (s->ep != HANDLE_INVALID)
        sys_handle_close(s->ep);

    memset(s, 0, sizeof(*s));
}

int session_active_count(void) {
    int count = 0;
    for (int i = 0; i < SESSION_MAX; i++) {
        if (g_sessions[i].active)
            count++;
    }
    return count;
}

void session_fill_info(int idx, struct user_info *info) {
    memset(info, 0, sizeof(*info));
    struct session *s = session_get(idx);
    if (!s)
        return;
    info->uid = s->uid;
    info->gid = s->gid;
    strncpy(info->name, s->name, USER_NAME_MAX - 1);
    strncpy(info->home, s->home, USER_HOME_MAX - 1);
    strncpy(info->shell, s->shell, USER_SHELL_MAX - 1);
}

int session_fill_wait_set(handle_t *handles, int offset) {
    int count = 0;
    for (int i = 0; i < SESSION_MAX; i++) {
        if (g_sessions[i].active) {
            handles[offset + count] = g_sessions[i].ep;
            count++;
        }
    }
    return count;
}
