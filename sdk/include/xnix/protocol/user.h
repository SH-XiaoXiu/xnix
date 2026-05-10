/**
 * @file xnix/protocol/user.h
 * @brief userd 用户服务 IPC 协议定义
 *
 * 用户认证, 会话管理, 权限提升.
 *
 * 身份边界:
 *   - user_ep 只处理登录等全局入口.
 *   - 登录成功后 userd 返回 session endpoint handle.
 *   - 后续身份相关操作发到 session endpoint, userd 根据 endpoint
 *     本身确定调用者 session.
 */

#ifndef XNIX_PROTOCOL_USER_H
#define XNIX_PROTOCOL_USER_H

#include <xnix/abi/stdint.h>

/*
 * userd IPC 操作码
 */
#define USER_OP_LOGIN          0x6001 /* 认证用户 */
#define USER_OP_LOGIN_REPLY    0x6002
#define USER_OP_LOGOUT         0x6003 /* 注销会话 */
#define USER_OP_LOGOUT_REPLY   0x6004
#define USER_OP_WHOAMI         0x6005 /* 查询当前会话用户信息 */
#define USER_OP_WHOAMI_REPLY   0x6006
#define USER_OP_VALIDATE       0x6007 /* 验证会话 (供其他服务使用) */
#define USER_OP_VALIDATE_REPLY 0x6008
#define USER_OP_SUDO           0x6009 /* 认证并签发目标用户临时会话 */
#define USER_OP_SUDO_REPLY     0x600A
#define USER_OP_ADDUSER        0x600B /* 创建新用户 */
#define USER_OP_ADDUSER_REPLY  0x600C
#define USER_OP_PASSWD         0x600D /* 修改密码 */
#define USER_OP_PASSWD_REPLY   0x600E

/*
 * 常量
 */
#define USER_NAME_MAX  32
#define USER_HOME_MAX  64
#define USER_SHELL_MAX 64
#define USER_PASS_MAX  32

/*
 * 用户信息 (嵌入回复 buffer)
 */
struct user_info {
    uint32_t uid;
    uint32_t gid;
    char     name[USER_NAME_MAX];
    char     home[USER_HOME_MAX];
    char     shell[USER_SHELL_MAX];
};

/*
 * LOGIN 请求 (通过 buffer 传递)
 *
 * 回复:
 *   regs[0] = USER_OP_LOGIN_REPLY
 *   regs[1] = 0 成功, <0 错误 (-EACCES, -ENOENT)
 *   handles[0] = session handle (成功时)
 *   buffer = struct user_info
 */
struct user_login_req {
    char username[USER_NAME_MAX];
    char password[USER_PASS_MAX];
};

/*
 * SUDO 请求 (发到 session endpoint, 通过 buffer 传递)
 *
 * 请求:
 *   regs[0] = USER_OP_SUDO
 *   regs[1] = target_uid (默认 0 = root)
 *   buffer  = struct user_sudo_req
 *
 * 回复:
 *   regs[0] = USER_OP_SUDO_REPLY
 *   regs[1] = 0 成功, <0 错误
 *   handles[0] = target session handle (成功时)
 *   buffer = struct user_info (成功时)
 */
struct user_sudo_req {
    char password[USER_PASS_MAX]; /* 调用者密码 (re-auth) */
};

/*
 * WHOAMI / VALIDATE 请求 (发到 session endpoint)
 *
 *   regs[0] = USER_OP_WHOAMI 或 USER_OP_VALIDATE
 *
 * 回复:
 *   regs[0] = USER_OP_WHOAMI_REPLY / USER_OP_VALIDATE_REPLY
 *   regs[1] = 0 成功, <0 错误
 *   buffer = struct user_info
 *
 * LOGOUT 请求 (发到 session endpoint)
 *   regs[0] = USER_OP_LOGOUT
 *
 * 回复:
 *   regs[0] = USER_OP_LOGOUT_REPLY
 *   regs[1] = 0 成功
 *
 * ADDUSER 请求 (发到 session endpoint, 需要 uid=0)
 *   regs[0] = USER_OP_ADDUSER
 *   buffer = struct user_adduser_req
 *
 * 回复:
 *   regs[0] = USER_OP_ADDUSER_REPLY
 *   regs[1] = 0 成功, <0 错误
 *   regs[2] = 分配的 uid
 */
struct user_adduser_req {
    char     username[USER_NAME_MAX];
    char     password[USER_PASS_MAX]; /* 明文, userd 内部哈希 */
    char     shell[USER_SHELL_MAX];   /* 空 = 默认 /bin/shell.elf */
};

/*
 * PASSWD 请求 (发到 session endpoint)
 *   regs[0] = USER_OP_PASSWD
 *   buffer  = struct user_passwd_req
 *
 * 回复:
 *   regs[0] = USER_OP_PASSWD_REPLY
 *   regs[1] = 0 成功, <0 错误
 */
struct user_passwd_req {
    char old_password[USER_PASS_MAX];
    char new_password[USER_PASS_MAX];
};

#endif /* XNIX_PROTOCOL_USER_H */
