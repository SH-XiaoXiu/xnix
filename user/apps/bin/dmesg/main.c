/**
 * @file main.c
 * @brief dmesg - 读取并显示内核日志
 *
 * 一次性读取 kmsg 中所有已有条目并输出到 stdout.
 */

#include <stdio.h>
#include <xnix/syscall.h>

#define KMSG_BUF_SIZE 512

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    uint32_t seq = 0;
    char     buf[KMSG_BUF_SIZE];

    while (1) {
        int ret = sys_kmsg_read(&seq, buf, sizeof(buf));
        if (ret <= 0) {
            break;
        }

        /* kmsg 格式: "<level>,<seq>,<timestamp>;text\n"
         * 找到分号后输出文本 */
        const char *text = buf;
        for (int i = 0; i < ret; i++) {
            if (buf[i] == ';') {
                text = &buf[i + 1];
                break;
            }
        }

        int text_len = ret - (int)(text - buf);
        if (text_len > 0) {
            /* 直接输出文本部分 */
            for (int i = 0; i < text_len; i++) {
                putchar(text[i]);
            }
        }
    }

    fflush(stdout);
    return 0;
}
