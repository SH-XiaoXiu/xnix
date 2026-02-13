/**
 * @file perm.h
 * @brief 权限 ABI 定义
 *
 * 定义了常用的权限节点常量.
 */

#ifndef XNIX_ABI_PERM_H
#define XNIX_ABI_PERM_H

#include <xnix/abi/stdint.h>

/* Permission node name constants (for userspace reference) */

/* IPC 发送权限 */
#define PERM_NODE_IPC_SEND "xnix.ipc.send"
/* IPC 接收权限 */
#define PERM_NODE_IPC_RECV "xnix.ipc.recv"
/* Endpoint 创建权限 */
#define PERM_NODE_IPC_ENDPOINT_CREATE "xnix.ipc.endpoint.create"

/* 进程创建 (spawn) 权限 */
#define PERM_NODE_PROCESS_SPAWN "xnix.process.spawn"
/* 进程执行 (exec) 权限 */
#define PERM_NODE_PROCESS_EXEC "xnix.process.exec"

/* Handle 授予权限(允许将 handle 传给其他进程) */
#define PERM_NODE_HANDLE_GRANT "xnix.handle.grant"

/* I/O 端口访问通配符 */
#define PERM_NODE_IO_PORT_ALL "xnix.io.port.*"

/* 内存映射权限 */
#define PERM_NODE_MM_MMAP "xnix.mm.mmap"

/* Limits */
/* 权限节点名称最大长度 */
#define PERM_NODE_NAME_MAX 128

/* Profile 动态创建参数 */
#define ABI_PERM_RULE_MAX    16   /* 单个 profile 最多 16 条规则 */
#define ABI_PERM_NODE_MAX    64   /* 权限节点名最大长度 */

struct abi_perm_rule {
    char     node[ABI_PERM_NODE_MAX]; /* 如 "xnix.ipc.*" */
    uint32_t value;                   /* 0=DENY, 1=GRANT */
};

struct abi_profile_create_args {
    char                 name[32];                      /* Profile 名称 */
    char                 parent[32];                    /* 父 profile 名(空=无继承)*/
    uint32_t             rule_count;
    struct abi_perm_rule rules[ABI_PERM_RULE_MAX];
};

#endif /* XNIX_ABI_PERM_H */
