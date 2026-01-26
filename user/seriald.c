#include <xnix/console_udm.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

#define BOOT_CONSOLE_EP        ((uint32_t)0)
#define BOOT_SERIAL_IOPORT_CAP ((uint32_t)1)

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

static inline int syscall2(int num, uint32_t arg1, uint32_t arg2) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2));
    return ret;
}

static inline int syscall3(int num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3));
    return ret;
}

void sys_exit(int code) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_EXIT), "b"(code));
    (void)ret;
    while (1) {
    }
}

static inline int sys_ioport_outb(uint32_t io_cap, uint16_t port, uint8_t val) {
    return syscall3(SYS_IOPORT_OUTB, io_cap, (uint32_t)port, (uint32_t)val);
}

static inline int sys_ioport_inb(uint32_t io_cap, uint16_t port) {
    return syscall2(SYS_IOPORT_INB, io_cap, (uint32_t)port);
}

static inline int sys_ipc_receive(uint32_t ep, struct ipc_message *msg, uint32_t timeout_ms) {
    return syscall3(SYS_IPC_RECV, ep, (uint32_t)(uintptr_t)msg, timeout_ms);
}

static void serial_init(void) {
    sys_ioport_outb(BOOT_SERIAL_IOPORT_CAP, COM1 + REG_INTR_ENABLE, 0x00);
    sys_ioport_outb(BOOT_SERIAL_IOPORT_CAP, COM1 + REG_LINE_CTRL, 0x80);
    sys_ioport_outb(BOOT_SERIAL_IOPORT_CAP, COM1 + REG_DIVISOR_LO, 0x03);
    sys_ioport_outb(BOOT_SERIAL_IOPORT_CAP, COM1 + REG_DIVISOR_HI, 0x00);
    sys_ioport_outb(BOOT_SERIAL_IOPORT_CAP, COM1 + REG_LINE_CTRL, 0x03);
    sys_ioport_outb(BOOT_SERIAL_IOPORT_CAP, COM1 + REG_FIFO_CTRL, 0xC7);
    sys_ioport_outb(BOOT_SERIAL_IOPORT_CAP, COM1 + REG_MODEM_CTRL, 0x0B);
}

static void serial_putc(char c) {
    while (1) {
        int lsr = sys_ioport_inb(BOOT_SERIAL_IOPORT_CAP, COM1 + REG_LINE_STATUS);
        if (lsr >= 0 && (lsr & LSR_TX_EMPTY)) {
            break;
        }
    }
    sys_ioport_outb(BOOT_SERIAL_IOPORT_CAP, COM1 + REG_DATA, (uint8_t)c);
}

static void serial_puts(const char *s) {
    while (s && *s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

static void serial_set_color(uint32_t color) {
    static const char *ansi_colors[] = {
        "\033[30m", "\033[34m", "\033[32m", "\033[36m", "\033[31m", "\033[35m",
        "\033[33m", "\033[37m", "\033[90m", "\033[94m", "\033[92m", "\033[96m",
        "\033[91m", "\033[95m", "\033[93m", "\033[97m",
    };

    if (color <= 15) {
        serial_puts(ansi_colors[color]);
    }
}

static void serial_reset_color(void) {
    serial_puts("\033[0m");
}

static void serial_clear(void) {
    serial_puts("\033[2J\033[H");
}

int main(void) {
    serial_init();

    struct ipc_message msg;
    while (1) {
        __builtin_memset(&msg, 0, sizeof(msg));
        msg.buffer.data = 0;
        msg.buffer.size = 0;

        if (sys_ipc_receive(BOOT_CONSOLE_EP, &msg, 0) < 0) {
            continue;
        }

        uint32_t op = msg.regs.data[0];
        switch (op) {
        case CONSOLE_UDM_OP_PUTC:
            serial_putc((char)(msg.regs.data[1] & 0xFF));
            break;
        case CONSOLE_UDM_OP_SET_COLOR:
            serial_set_color(msg.regs.data[1]);
            break;
        case CONSOLE_UDM_OP_RESET_COLOR:
            serial_reset_color();
            break;
        case CONSOLE_UDM_OP_CLEAR:
            serial_clear();
            break;
        default:
            break;
        }
    }
}
