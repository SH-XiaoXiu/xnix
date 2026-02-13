/**
 * @file main.c
 * @brief sudo: 权限提升客户端
 *
 * Usage: sudo [--profile=<name>] <command> [args...]
 *
 * 通过 sudod 以提升的 profile 执行指定命令.
 */

#include <d/protocol/sudo.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/proc.h>
#include <xnix/syscall.h>

/* 简单的 PATH 搜索 */
static int find_in_path(const char *name, char *out, size_t out_size) {
    static const char *paths[] = {"/bin", "/sbin", "/mnt/bin", NULL};

    /* 如果已经是绝对路径 */
    if (name[0] == '/') {
        struct vfs_stat st;
        if (vfs_stat(name, &st) == 0) {
            size_t len = strlen(name);
            if (len >= out_size) {
                len = out_size - 1;
            }
            memcpy(out, name, len);
            out[len] = '\0';
            return 0;
        }
        return -1;
    }

    for (int i = 0; paths[i]; i++) {
        snprintf(out, out_size, "%s/%s", paths[i], name);
        struct vfs_stat st;
        if (vfs_stat(out, &st) == 0) {
            return 0;
        }
        /* 尝试加 .elf 后缀 */
        snprintf(out, out_size, "%s/%s.elf", paths[i], name);
        if (vfs_stat(out, &st) == 0) {
            return 0;
        }
    }

    return -1;
}

int main(int argc, char **argv) {
    const char *profile   = "sudo"; /* 默认提升到 sudo profile */
    int         cmd_start = 1;

    if (argc > 1 && strncmp(argv[1], "--profile=", 10) == 0) {
        profile   = argv[1] + 10;
        cmd_start = 2;
    }

    if (cmd_start >= argc) {
        printf("Usage: sudo [--profile=<name>] <command> [args...]\n");
        return 1;
    }

    env_set_name("sudo");
    handle_t sudo_ep = env_require("sudo_ep");
    if (sudo_ep == HANDLE_INVALID) {
        return 1;
    }

    /* 初始化 VFS 客户端 */
    handle_t vfs_ep = env_get_handle("vfs_ep");
    if (vfs_ep != HANDLE_INVALID) {
        vfs_client_init(vfs_ep);
    }

    /* 路径解析 */
    char path[ABI_EXEC_PATH_MAX];
    if (find_in_path(argv[cmd_start], path, sizeof(path)) < 0) {
        printf("sudo: %s: command not found\n", argv[cmd_start]);
        return 1;
    }

    /* 使用 proc_builder 构建 exec_args */
    struct proc_builder b;
    proc_new(&b, path);
    proc_set_profile(&b, profile);
    proc_inherit_named(&b);
    for (int i = cmd_start; i < argc; i++) {
        proc_add_arg(&b, argv[i]);
    }

    /* 通过 IPC 发送请求给 sudod */
    struct ipc_message msg = {0};
    msg.regs.data[0]       = SUDO_OP_EXEC;
    msg.buffer.data        = (uint64_t)(uintptr_t)&b.args;
    msg.buffer.size        = sizeof(b.args);

    struct ipc_message reply = {0};
    int                ret   = sys_ipc_call(sudo_ep, &msg, &reply, 5000);
    if (ret < 0) {
        printf("sudo: request failed: %s\n", strerror(-ret));
        return 1;
    }

    int pid = (int)reply.regs.data[1];
    if (pid < 0) {
        printf("sudo: exec failed: %s\n", strerror(-pid));
        return 1;
    }

    /* 等待子进程退出 */
    int status;
    sys_waitpid(pid, &status, 0);
    return status;
}
