/**
 * @file xnix/protocol/user.h
 * @brief userd 用户服务 IPC 协议定义
 *
 * 用户认证, 会话管理, 权限提升.
 */

#ifndef XNIX_PROTOCOL_USER_H
#define XNIX_PROTOCOL_USER_H

#include <xnix/abi/process.h>
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
#define USER_OP_SUDO           0x6009 /* 以目标用户身份执行命令 */
#define USER_OP_SUDO_REPLY     0x600A

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
 *   regs[1] = pid (>0 成功, <0 错误)
 */
struct user_sudo_req {
    char                password[USER_PASS_MAX]; /* 调用者密码 (re-auth) */
    struct abi_exec_args exec_args;              /* 要执行的命令 */
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
 */

#endif /* XNIX_PROTOCOL_USER_H */
