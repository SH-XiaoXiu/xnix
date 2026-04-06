/**
 * @file ps2_mouse.c
 * @brief PS/2 鼠标硬件协议实现
 */

#include "ps2_mouse.h"

#include <xnix/syscall.h>

#define PS2_CMD_READ_CONFIG  0x20
#define PS2_CMD_WRITE_CONFIG 0x60
#define PS2_CMD_ENABLE_AUX   0xA8
#define PS2_CMD_WRITE_AUX    0xD4

#define MOUSE_CMD_RESET         0xFF
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_ENABLE_REPORT 0xF4

#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02

#define PS2_ACK 0xFA

static int ps2_wait_input(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(sys_ioport_inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return 0;
        }
    }
    return -1;
}

static int ps2_wait_output(void) {
    for (int i = 0; i < 100000; i++) {
        if (sys_ioport_inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return 0;
        }
    }
    return -1;
}

static int ps2_read_data(void) {
    if (ps2_wait_output() < 0) {
        return -1;
    }
    return sys_ioport_inb(PS2_DATA_PORT);
}

static int ps2_mouse_write(uint8_t cmd) {
    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_AUX);

    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_DATA_PORT, cmd);

    int ack = ps2_read_data();
    if (ack != PS2_ACK) {
        return -1;
    }
    return 0;
}

int ps2_mouse_init(void) {
    int timeout = 1024;
    while ((sys_ioport_inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) && --timeout > 0) {
        (void)sys_ioport_inb(PS2_DATA_PORT);
    }

    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_COMMAND_PORT, PS2_CMD_ENABLE_AUX);

    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);
    int config = ps2_read_data();
    if (config < 0) {
        return -1;
    }

    config |= (1 << 0);
    config |= (1 << 1);
    config &= ~(1 << 4);
    config &= ~(1 << 5);

    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);
    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_DATA_PORT, (uint8_t)config);

    if (ps2_mouse_write(MOUSE_CMD_RESET) < 0) {
        return -1;
    }
    ps2_read_data();
    ps2_read_data();

    if (ps2_mouse_write(MOUSE_CMD_SET_DEFAULTS) < 0) {
        return -1;
    }
    if (ps2_mouse_write(MOUSE_CMD_ENABLE_REPORT) < 0) {
        return -1;
    }
    return 0;
}

int ps2_mouse_parse(const uint8_t raw[3], int16_t *dx, int16_t *dy, uint8_t *btns) {
    uint8_t flags = raw[0];

    if (!(flags & 0x08)) {
        return -1;
    }

    if (flags & 0xC0) {
        *dx   = 0;
        *dy   = 0;
        *btns = flags & 0x07;
        return 0;
    }

    int16_t x = (int16_t)raw[1];
    int16_t y = (int16_t)raw[2];

    if (flags & 0x10) {
        x |= 0xFF00;
    }
    if (flags & 0x20) {
        y |= 0xFF00;
    }

    *dx   = x;
    *dy   = -y;
    *btns = flags & 0x07;
    return 0;
}
