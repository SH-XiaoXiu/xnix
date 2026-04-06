/**
 * @file main.c
 * @brief sudo: 权限提升客户端
 *
 * Usage: sudo <command> [args...]
 *
 * 通过 sudod 以提升的能力执行指定命令.
 */

#include <xnix/protocol/sudo.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <vfs_client.h>
#include <spawn.h>
#include <xnix/abi/process.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: sudo <command> [args...]\n");
        return 1;
    }

    handle_t sudo_ep = env_require("sudo_ep");
    if (sudo_ep == HANDLE_INVALID) {
        return 1;
    }

    /* 初始化 VFS 客户端 */
    handle_t vfs_ep = env_get_handle("vfs_ep");
    if (vfs_ep != HANDLE_INVALID) {
        vfs_client_init(vfs_ep);
    }

    struct abi_exec_args exec_args;
    int build_ret = posix_spawnp_make_exec_args(&exec_args, argv[1], argc - 1,
                                                (const char **)&argv[1]);
    if (build_ret < 0) {
        if (build_ret == -ENOENT) {
            printf("sudo: %s: command not found\n", argv[1]);
        } else {
            printf("sudo: failed to build exec request: %s\n", strerror(-build_ret));
        }
        return 1;
    }

    /* 通过 IPC 发送请求给 sudod */
    struct ipc_message msg = {0};
    msg.regs.data[0]       = SUDO_OP_EXEC;
    msg.buffer.data        = (uint64_t)(uintptr_t)&exec_args;
    msg.buffer.size        = sizeof(exec_args);

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
    waitpid(pid, &status, 0);
    return status;
}
