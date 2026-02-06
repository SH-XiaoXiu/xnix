/**
 * @file main.c
 * @brief 内核日志转发服务 (klogd)
 *
 * 循环读取 kmsg 内核日志,转发到 tty 终端输出.
 */

#include <d/protocol/tty.h>
#include <stdio.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

#include <unistd.h>

#define KMSG_BUF_SIZE 512

static handle_t g_tty_ep = HANDLE_INVALID;

/**
 * 通过 TTY IPC 输出字符串
 */
static void tty_write(const char *s, uint32_t len) {
    if (g_tty_ep == HANDLE_INVALID || len == 0) {
        return;
    }

    struct ipc_message msg;
    memset(&msg, 0, sizeof(msg));
    msg.regs.data[0] = TTY_OP_WRITE;
    msg.regs.data[1] = len;
    msg.buffer.data   = (void *)s;
    msg.buffer.size   = len;

    sys_ipc_send(g_tty_ep, &msg, 100);
}

/**
 * 输出一行 kmsg 条目
 *
 * kmsg_read 输出格式: "<level>,<seq>,<timestamp>;text\n"
 * 我们直接转发文本部分
 */
static void output_entry(const char *entry, int len) {
    /* 找到分号后的文本部分 */
    const char *text = entry;
    for (int i = 0; i < len; i++) {
        if (entry[i] == ';') {
            text = &entry[i + 1];
            break;
        }
    }

    int text_len = len - (int)(text - entry);
    if (text_len > 0) {
        tty_write(text, (uint32_t)text_len);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* 获取 tty endpoint */
    g_tty_ep = env_get_handle("tty1");
    if (g_tty_ep == HANDLE_INVALID) {
        g_tty_ep = env_get_handle("tty0");
    }

    /* 从 seq 0 开始,先读取已有的日志 */
    uint32_t seq = 0;
    char buf[KMSG_BUF_SIZE];

    /* 读取积压的历史日志 */
    while (1) {
        int ret = sys_kmsg_read(&seq, buf, sizeof(buf));
        if (ret <= 0) {
            break;
        }
        output_entry(buf, ret);
    }

    /* 主循环:轮询新日志 */
    while (1) {
        int ret = sys_kmsg_read(&seq, buf, sizeof(buf));
        if (ret > 0) {
            output_entry(buf, ret);
        } else {
            /* 无新条目,睡眠后重试 */
            msleep(100);
        }
    }

    return 0;
}
