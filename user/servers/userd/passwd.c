/**
 * @file passwd.c
 * @brief /etc/passwd 解析和密码验证
 *
 * 格式: username:sha256hex:uid:gid:home:shell
 * 空 hash 字段 = 无密码 (空密码即可登录)
 */

#include "passwd.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct passwd_entry g_users[PASSWD_MAX_USERS];
static int                 g_user_count;

/* 简单 atoi (libc 可能没有完整版) */
static uint32_t parse_uint(const char *s) {
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return val;
}

/**
 * 解析一行 passwd
 * @return 0 成功, <0 失败
 */
static int parse_line(const char *line, struct passwd_entry *ent) {
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* 去尾部换行 */
    char *nl = strchr(buf, '\n');
    if (nl)
        *nl = '\0';
    nl = strchr(buf, '\r');
    if (nl)
        *nl = '\0';

    /* 跳过空行和注释 */
    if (buf[0] == '\0' || buf[0] == '#')
        return -1;

    /* 按 ':' 分割 */
    char *fields[6];
    char *p = buf;

    for (int i = 0; i < 6; i++) {
        fields[i] = p;
        if (i < 5) {
            char *sep = strchr(p, ':');
            if (!sep)
                return -1;
            *sep = '\0';
            p    = sep + 1;
        }
    }

    strncpy(ent->name, fields[0], USER_NAME_MAX - 1);
    strncpy(ent->hash, fields[1], PASSWD_HASH_SIZE - 1);
    ent->uid = parse_uint(fields[2]);
    ent->gid = parse_uint(fields[3]);
    strncpy(ent->home, fields[4], USER_HOME_MAX - 1);
    strncpy(ent->shell, fields[5], USER_SHELL_MAX - 1);
    ent->valid = true;

    return 0;
}

int passwd_load(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("[userd] cannot open %s\n", path);
        return -1;
    }

    /* 读取整个文件 (passwd 文件很小) */
    char file_buf[2048];
    int  total = 0;
    int  n;
    while (total < (int)sizeof(file_buf) - 1 &&
           (n = read(fd, file_buf + total, sizeof(file_buf) - 1 - total)) > 0) {
        total += n;
    }
    close(fd);
    file_buf[total] = '\0';

    /* 逐行解析 */
    g_user_count = 0;
    char *line = file_buf;
    while (*line && g_user_count < PASSWD_MAX_USERS) {
        char *eol = strchr(line, '\n');
        if (eol)
            *eol = '\0';

        struct passwd_entry ent = {0};
        if (parse_line(line, &ent) == 0) {
            g_users[g_user_count++] = ent;
        }

        if (!eol)
            break;
        line = eol + 1;
    }

    printf("[userd] loaded %d users from %s\n", g_user_count, path);
    return g_user_count;
}

struct passwd_entry *passwd_lookup(const char *name) {
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i].valid && strcmp(g_users[i].name, name) == 0)
            return &g_users[i];
    }
    return NULL;
}

struct passwd_entry *passwd_lookup_uid(uint32_t uid) {
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i].valid && g_users[i].uid == uid)
            return &g_users[i];
    }
    return NULL;
}

bool passwd_verify(struct passwd_entry *ent, const char *password) {
    if (!ent)
        return false;

    /* 空 hash = 无密码 */
    if (ent->hash[0] == '\0') {
        return (password[0] == '\0');
    }

    /* 计算输入密码的 SHA-256 并比较 */
    char hex[SHA256_HEX_SIZE];
    sha256_hex(password, strlen(password), hex);

    return strcmp(hex, ent->hash) == 0;
}

void passwd_fill_info(struct passwd_entry *ent, struct user_info *info) {
    memset(info, 0, sizeof(*info));
    if (!ent)
        return;
    info->uid = ent->uid;
    info->gid = ent->gid;
    strncpy(info->name, ent->name, USER_NAME_MAX - 1);
    strncpy(info->home, ent->home, USER_HOME_MAX - 1);
    strncpy(info->shell, ent->shell, USER_SHELL_MAX - 1);
}

int passwd_change_password(struct passwd_entry *ent, const char *new_password) {
    if (!ent)
        return -EINVAL;
    if (new_password[0] == '\0')
        ent->hash[0] = '\0';
    else
        sha256_hex(new_password, strlen(new_password), ent->hash);
    return 0;
}

uint32_t passwd_next_uid(void) {
    uint32_t max_uid = 999;
    for (int i = 0; i < g_user_count; i++) {
        if (g_users[i].valid && g_users[i].uid > max_uid)
            max_uid = g_users[i].uid;
    }
    return max_uid + 1;
}

int passwd_add(const char *name, const char *password,
               uint32_t uid, uint32_t gid,
               const char *home, const char *shell) {
    if (g_user_count >= PASSWD_MAX_USERS)
        return -ENOMEM;
    if (passwd_lookup(name))
        return -EEXIST;
    if (passwd_lookup_uid(uid))
        return -EEXIST;

    struct passwd_entry *ent = &g_users[g_user_count];
    memset(ent, 0, sizeof(*ent));
    strncpy(ent->name, name, USER_NAME_MAX - 1);
    ent->uid = uid;
    ent->gid = gid;
    strncpy(ent->home, home, USER_HOME_MAX - 1);
    strncpy(ent->shell, shell, USER_SHELL_MAX - 1);
    ent->valid = true;

    /* 哈希密码 (空密码 = 空 hash) */
    if (password[0] != '\0')
        sha256_hex(password, strlen(password), ent->hash);

    g_user_count++;
    return 0;
}

int passwd_save(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0)
        return -1;

    for (int i = 0; i < g_user_count; i++) {
        if (!g_users[i].valid)
            continue;
        struct passwd_entry *e = &g_users[i];
        char line[512];
        int  len = snprintf(line, sizeof(line), "%s:%s:%u:%u:%s:%s\n",
                            e->name, e->hash, e->uid, e->gid, e->home, e->shell);
        if (len > 0)
            write(fd, line, (size_t)len);
    }

    close(fd);
    return 0;
}
