/**
 * @file passwd.h
 * @brief /etc/passwd 解析和密码验证
 */

#ifndef USERD_PASSWD_H
#define USERD_PASSWD_H

#include <stdbool.h>
#include <stdint.h>
#include <xnix/protocol/user.h>
#include <xnix/sha256.h>

#define PASSWD_MAX_USERS 16
#define PASSWD_HASH_SIZE SHA256_HEX_SIZE

struct passwd_entry {
    char     name[USER_NAME_MAX];
    char     hash[PASSWD_HASH_SIZE]; /* SHA-256 hex, 空 = 无密码 */
    uint32_t uid;
    uint32_t gid;
    char     home[USER_HOME_MAX];
    char     shell[USER_SHELL_MAX];
    bool     valid;
};

/**
 * 从 /etc/passwd 加载用户数据库
 * @return 加载的用户数
 */
int passwd_load(const char *path);

/**
 * 按用户名查找
 * @return 条目指针或 NULL
 */
struct passwd_entry *passwd_lookup(const char *name);

/**
 * 按 UID 查找
 */
struct passwd_entry *passwd_lookup_uid(uint32_t uid);

/**
 * 验证密码
 * @return true 匹配
 */
bool passwd_verify(struct passwd_entry *ent, const char *password);

/**
 * 填充 user_info 结构
 */
void passwd_fill_info(struct passwd_entry *ent, struct user_info *info);

/**
 * 添加用户
 * @param password 明文密码 (内部哈希)
 * @return 0 成功, <0 错误
 */
int passwd_add(const char *name, const char *password,
               uint32_t uid, uint32_t gid,
               const char *home, const char *shell);

/**
 * 分配下一个可用 UID (从 1000 开始)
 */
uint32_t passwd_next_uid(void);

/**
 * 修改密码
 * @return 0 成功, <0 错误
 */
int passwd_change_password(struct passwd_entry *ent, const char *new_password);

/**
 * 保存当前用户数据库到文件
 */
int passwd_save(const char *path);

#endif /* USERD_PASSWD_H */
