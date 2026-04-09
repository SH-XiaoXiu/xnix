/**
 * @file session.h
 * @brief 用户会话管理
 *
 * 每个会话拥有一个 IPC endpoint. userd 监听所有 session endpoint,
 * 消息到达哪个 endpoint 就知道属于哪个会话, 无需句柄比较.
 */

#ifndef USERD_SESSION_H
#define USERD_SESSION_H

#include <stdbool.h>
#include <xnix/abi/handle.h>
#include <xnix/protocol/user.h>

#define SESSION_MAX 16

struct session {
    bool     active;
    handle_t ep;       /* session endpoint (userd 持有) */
    uint32_t uid;
    uint32_t gid;
    char     name[USER_NAME_MAX];
    char     home[USER_HOME_MAX];
    char     shell[USER_SHELL_MAX];
};

/**
 * 初始化会话管理器
 */
void session_init(void);

/**
 * 创建新会话
 * @return 会话索引, <0 失败
 */
int session_create(uint32_t uid, uint32_t gid,
                   const char *name, const char *home, const char *shell);

/**
 * 按 endpoint handle 查找会话
 * @return 会话索引, -1 未找到
 */
int session_find_by_ep(handle_t ep);

/**
 * 获取会话
 */
struct session *session_get(int idx);

/**
 * 销毁会话
 */
void session_destroy(int idx);

/**
 * 获取活跃会话数
 */
int session_active_count(void);

/**
 * 填充 user_info
 */
void session_fill_info(int idx, struct user_info *info);

/**
 * 将所有活跃 session endpoint 添加到 wait set
 * @param handles 输出数组
 * @param offset  从此偏移开始写入
 * @return 写入数量
 */
int session_fill_wait_set(handle_t *handles, int offset);

#endif /* USERD_SESSION_H */
