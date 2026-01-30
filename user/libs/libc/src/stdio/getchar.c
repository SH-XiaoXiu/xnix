/**
 * @file getchar.c
 * @brief 标准输入函数
 */

#include <stdio.h>
#include <xnix/syscall.h>

int getchar(void) {
    return sys_input_read();
}

char *gets_s(char *buf, size_t size) {
    if (!buf || size == 0) {
        return NULL;
    }

    size_t pos = 0;
    size_t max = size - 1; /* 留一个位置给 '\0' */

    while (pos < max) {
        int c = getchar();
        if (c < 0) {
            break;
        }

        if (c == '\n' || c == '\r') {
            /* 回车,结束输入 */
            putchar('\n');
            break;
        }
        if (c == '\b' || c == 127) {
            /* 退格 */
            if (pos > 0) {
                pos--;
                /* 回显:退格 + 空格 + 退格 */
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c >= 32 && c < 127) {
            /* 可打印字符 */
            buf[pos++] = (char)c;
            putchar(c);
        }
        /* 忽略其他控制字符 */
    }

    buf[pos] = '\0';
    return buf;
}
