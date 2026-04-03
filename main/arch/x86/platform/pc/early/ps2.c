/**
 * @file ps2.c
 * @brief PS/2 控制器驱动(键盘)
 *
 * 处理 IRQ1(键盘中断),将扫描码传递给用户态.
 */

#include <arch/cpu.h>

#include <xnix/irq.h>
#include <xnix/stdio.h>

#define PS2_DATA_PORT   0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT    0x64

#define PS2_STATUS_OUTPUT_FULL 0x01

#define IRQ_KEYBOARD 1
#define IRQ_MOUSE    12

static void ps2_kbd_irq_handler(irq_frame_t *frame) {
    (void)frame;

    uint8_t status = inb(PS2_STATUS_PORT);
    if (!(status & PS2_STATUS_OUTPUT_FULL)) {
        return;
    }

    uint8_t scancode = inb(PS2_DATA_PORT);
    irq_user_push(IRQ_KEYBOARD, scancode);
}

static void ps2_mouse_irq_handler(irq_frame_t *frame) {
    (void)frame;

    uint8_t status = inb(PS2_STATUS_PORT);
    if (!(status & PS2_STATUS_OUTPUT_FULL)) {
        return;
    }

    uint8_t data = inb(PS2_DATA_PORT);
    irq_user_push(IRQ_MOUSE, data);
}

static void ps2_init(void) {
    /* 清空残留数据,避免 IRQ 线卡在高电平 */
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        (void)inb(PS2_DATA_PORT);
    }

    irq_set_handler(IRQ_KEYBOARD, ps2_kbd_irq_handler);
    irq_set_handler(IRQ_MOUSE, ps2_mouse_irq_handler);

    pr_info("ps2: keyboard and mouse drivers initialized");
}

void ps2_register(void) {
    ps2_init();
}
