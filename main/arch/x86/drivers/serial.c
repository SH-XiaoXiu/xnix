/**
 * @file serial.c
 * @brief x86 串口驱动 (8250/16550) - 同步/异步双模式
 *
 * 早期启动阶段: 同步直接输出
 * 调度器就绪后: 异步通过 ring buffer 输出
 */

#include <arch/cpu.h>

#include <asm/irq_defs.h>
#include <kernel/irq/irq.h>
#include <xnix/console.h>
#include <xnix/sync.h>
#include <xnix/thread.h>

/* 串口输出锁,保护多核同时输出 */
static spinlock_t serial_lock = SPINLOCK_INIT;

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

/* 声明紧急输出注册函数 */
extern void console_register_emergency_putc(void (*putc)(char c));

/* 直接写硬件 */
static void serial_putc_hw(char c) {
    while ((inb(COM1 + REG_LINE_STATUS) & LSR_TX_EMPTY) == 0);
    outb(COM1 + REG_DATA, (uint8_t)c);
}

/* 同步输出(带锁保护,防止多核乱序) */
static void serial_putc_sync(char c) {
    uint32_t flags = spin_lock_irqsave(&serial_lock);
    if (c == '\n') {
        serial_putc_hw('\r');
    }
    serial_putc_hw(c);
    spin_unlock_irqrestore(&serial_lock, flags);
}

static void serial_puts_sync(const char *s) {
    uint32_t flags = spin_lock_irqsave(&serial_lock);
    while (*s) {
        if (*s == '\n') {
            serial_putc_hw('\r');
        }
        serial_putc_hw(*s++);
    }
    spin_unlock_irqrestore(&serial_lock, flags);
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

#define IRQ_SERIAL 4

static void serial_irq_handler(struct irq_regs *frame) {
    (void)frame;
    /* 检查 LSR 寄存器确定是否有接收到的数据 */
    if (inb(COM1 + REG_LINE_STATUS) & 0x01) {
        uint8_t data = inb(COM1 + REG_DATA);
        /* 将数据推送给用户态绑定的读取者 */
        irq_user_push(IRQ_SERIAL, data);
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

    /* 注册 IRQ4 处理函数 */
    irq_set_handler(IRQ_SERIAL, serial_irq_handler);

    /* 注册紧急输出函数,用于 panic 时直接输出 */
    console_register_emergency_putc(serial_putc_hw);
}

static void serial_start_consumer(void);

static struct console serial_console = {
    .name           = "serial",
    .flags          = CONSOLE_SYNC, /* 启动时同步输出,之后切换为异步 */
    .init           = serial_init,
    .putc           = serial_putc_sync,
    .puts           = serial_puts_sync,
    .set_color      = NULL, /* 颜色通过 ANSI 序列处理 */
    .reset_color    = NULL, /* 颜色通过 ANSI 序列处理 */
    .clear          = NULL,
    .start_consumer = serial_start_consumer,
};

static void serial_start_consumer(void) {
    thread_t t = thread_create("serial_out", serial_consumer_thread, NULL);
    if (!t) {
        return;
    }

    serial_console.flags = CONSOLE_ASYNC;
    serial_console.putc  = NULL;
    serial_console.puts  = NULL;
}

void serial_console_register(void) {
    console_register(&serial_console);
}
