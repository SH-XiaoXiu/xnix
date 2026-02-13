/**
 * @file main.c
 * @brief sudod: 权限提升守护进程
 *
 * 由 init 以 "sudo" profile (xnix.*) 启动.
 * 监听 "sudo_ep" endpoint, 接收执行请求并以指定 profile 创建进程.
 *
 * 当前为原型实现: 允许所有请求, 无认证检查.
 */

#include <d/protocol/sudo.h>
#include <stdio.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/abi/process.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    handle_t ep = env_get_handle("sudo_ep");
    if (ep == HANDLE_INVALID) {
        printf("sudod: sudo_ep not found\n");
        return 1;
    }

    /* 初始化 VFS 客户端(sys_exec 需要通过 VFS 读取 ELF) */
    handle_t vfs_ep = env_get_handle("vfs_ep");
    if (vfs_ep != HANDLE_INVALID) {
        vfs_client_init(vfs_ep);
    }

    svc_notify_ready("sudod");

    while (1) {
        struct ipc_message req = {0};
        char               buf[sizeof(struct abi_exec_args)];
        req.buffer.data = (uint64_t)(uintptr_t)buf;
        req.buffer.size = sizeof(buf);

        int ret = sys_ipc_receive(ep, &req, 0);
        if (ret < 0) {
            continue;
        }

        if (req.regs.data[0] == SUDO_OP_EXEC) {
            struct abi_exec_args *exec_args = (struct abi_exec_args *)buf;

            /*
             * TODO: 认证检查
             * 当前原型: 允许所有请求
             * 未来: 密码验证,policy 检查
             */

            /* 设置继承标志让子进程获得 stdio */
            exec_args->flags |= ABI_EXEC_INHERIT_STDIO;

            int pid = sys_exec(exec_args);

            /* 回复 */
            struct ipc_message reply = {0};
            reply.regs.data[0]       = SUDO_OP_EXEC_REPLY;
            reply.regs.data[1]       = (uint32_t)pid;
            sys_ipc_reply_to(req.sender_tid, &reply);
        }
    }

    return 0;
}
