/**
 * @file main.c
 * @brief kbd UDM 驱动
 *
 * 从 PS/2 控制器读取扫描码,翻译成字符.
 * 当前版本为回显模式,用于验证键盘输入功能.
 */

#include "scancode.h"

#include <stdio.h>
#include <xnix/syscall.h>
#include <xnix/udm/kbd.h>

int main(void) {
    printf("[kbd] keyboard driver starting\n");

    /* 绑定 IRQ1(无 notification) */
    int ret = sys_irq_bind(IRQ_KEYBOARD, -1, 0);
    if (ret < 0) {
        printf("[kbd] failed to bind IRQ1: %d\n", ret);
        return 1;
    }

    printf("[kbd] IRQ1 bound, type to test:\n");

    /* 主循环:阻塞读取扫描码,翻译,回显 */
    while (1) {
        uint8_t scancode;

        /* 阻塞读取 IRQ 数据 */
        ret = sys_irq_read(IRQ_KEYBOARD, &scancode, 1, 0);
        if (ret <= 0) {
            continue;
        }

        /* 翻译扫描码 */
        int c = scancode_to_char(scancode);
        if (c >= 0) {
            /* 写入内核输入队列 */
            sys_input_write((char)c);
        }
    }

    return 0;
}
