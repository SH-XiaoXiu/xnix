/**
 * @file serial.c
 * @brief x86 串口驱动 (8250/16550) - 异步输出
 *
 * 从 ring buffer 消费数据,通过后台线程输出到串口.
 * VGA 输出不被串口速度拖慢.
 */

#include <arch/cpu.h>

#include <xnix/console.h>
#include <xnix/thread.h>

#define COM1 0x3F8

#define REG_DATA        0
#define REG_INTR_ENABLE 1
#define REG_DIVISOR_LO  0
#define REG_DIVISOR_HI  1
#define REG_FIFO_CTRL   2
#define REG_LINE_CTRL   3
#define REG_MODEM_CTRL  4
#define REG_LINE_STATUS 5
#define LSR_TX_EMPTY    0x20

/* 直接写硬件 */
static void serial_putc_hw(char c) {
    while ((inb(COM1 + REG_LINE_STATUS) & LSR_TX_EMPTY) == 0);
    outb(COM1 + REG_DATA, (uint8_t)c);
}

/* 消费者线程 */
static void serial_consumer_thread(void *arg) {
    (void)arg;
    char c;

    while (1) {
        if (console_ringbuf_get(&c) == 0) {
            if (c == '\n') {
                serial_putc_hw('\r');
            }
            serial_putc_hw(c);
        } else {
            thread_yield();
        }
    }
}

static void serial_init(void) {
    outb(COM1 + REG_INTR_ENABLE, 0x00);
    outb(COM1 + REG_LINE_CTRL, 0x80);
    outb(COM1 + REG_DIVISOR_LO, 0x03);
    outb(COM1 + REG_DIVISOR_HI, 0x00);
    outb(COM1 + REG_LINE_CTRL, 0x03);
    outb(COM1 + REG_FIFO_CTRL, 0xC7);
    outb(COM1 + REG_MODEM_CTRL, 0x0B);
}

static struct console serial_console = {
    .name        = "serial",
    .flags       = CONSOLE_ASYNC,
    .init        = serial_init,
    .putc        = NULL, /* 异步驱动通过 buffer 消费 */
    .puts        = NULL,
    .set_color   = NULL, /* 颜色通过 ANSI 序列在 buffer 中传递 */
    .reset_color = NULL,
    .clear       = NULL,
};

void serial_console_register(void) {
    console_register(&serial_console);
}

void serial_consumer_start(void) {
    thread_create("serial_out", serial_consumer_thread, NULL);
}
