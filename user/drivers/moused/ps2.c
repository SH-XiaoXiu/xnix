/**
 * @file ps2.c
 * @brief PS/2 鼠标硬件协议实现
 */

#include "ps2.h"

#include <xnix/protocol/mouse.h>
#include <xnix/syscall.h>

/* PS/2 控制器命令 */
#define PS2_CMD_READ_CONFIG  0x20
#define PS2_CMD_WRITE_CONFIG 0x60
#define PS2_CMD_ENABLE_AUX   0xA8
#define PS2_CMD_WRITE_AUX    0xD4

/* PS/2 鼠标命令 */
#define MOUSE_CMD_RESET         0xFF
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_ENABLE_REPORT 0xF4

/* PS/2 状态位 */
#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02

/* ACK */
#define PS2_ACK 0xFA

/**
 * 等待 PS/2 控制器可写(input buffer 空)
 */
static int ps2_wait_input(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(sys_ioport_inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return 0;
        }
    }
    return -1;
}

/**
 * 等待 PS/2 控制器有数据可读(output buffer 满)
 */
static int ps2_wait_output(void) {
    for (int i = 0; i < 100000; i++) {
        if (sys_ioport_inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return 0;
        }
    }
    return -1;
}

/**
 * 读取 PS/2 数据端口(带等待)
 */
static int ps2_read_data(void) {
    if (ps2_wait_output() < 0) {
        return -1;
    }
    return sys_ioport_inb(PS2_DATA_PORT);
}

/**
 * 发送命令到第二 PS/2 端口(鼠标)
 */
static int ps2_mouse_write(uint8_t cmd) {
    /* 告诉控制器下一个字节发给鼠标 */
    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_AUX);

    /* 发送命令 */
    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_DATA_PORT, cmd);

    /* 等待 ACK */
    int ack = ps2_read_data();
    if (ack != PS2_ACK) {
        return -1;
    }

    return 0;
}

int ps2_mouse_init(void) {
    /* 1. 启用第二 PS/2 端口 */
    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_COMMAND_PORT, PS2_CMD_ENABLE_AUX);

    /* 2. 读取控制器配置字节 */
    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);
    int config = ps2_read_data();
    if (config < 0) {
        return -1;
    }

    /* 3. 修改配置:启用第二端口中断(bit 1),清除第二端口禁用(bit 5) */
    config |= (1 << 1);  /* 启用 IRQ12 */
    config &= ~(1 << 5); /* 取消禁用第二端口时钟 */

    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);
    if (ps2_wait_input() < 0) {
        return -1;
    }
    sys_ioport_outb(PS2_DATA_PORT, (uint8_t)config);

    /* 4. 重置鼠标 */
    if (ps2_mouse_write(MOUSE_CMD_RESET) < 0) {
        return -1;
    }
    /* 消耗 reset 响应: 0xAA (自检通过) + 0x00 (设备 ID) */
    ps2_read_data(); /* 0xAA */
    ps2_read_data(); /* 0x00 */

    /* 5. 设置默认参数 */
    if (ps2_mouse_write(MOUSE_CMD_SET_DEFAULTS) < 0) {
        return -1;
    }

    /* 6. 启用数据报告 */
    if (ps2_mouse_write(MOUSE_CMD_ENABLE_REPORT) < 0) {
        return -1;
    }

    return 0;
}

int ps2_mouse_parse(const uint8_t raw[3], int16_t *dx, int16_t *dy,
                    uint8_t *btns) {
    uint8_t flags = raw[0];

    /* 同步检查: bit 3 必须为 1 */
    if (!(flags & 0x08)) {
        return -1;
    }

    /* 溢出检查 */
    if (flags & 0xC0) {
        *dx   = 0;
        *dy   = 0;
        *btns = flags & 0x07;
        return 0;
    }

    /* 提取位移(带符号扩展) */
    int16_t x = (int16_t)raw[1];
    int16_t y = (int16_t)raw[2];

    if (flags & 0x10) { /* X 符号位 */
        x |= 0xFF00;
    }
    if (flags & 0x20) { /* Y 符号位 */
        y |= 0xFF00;
    }

    *dx   = x;
    *dy   = -y; /* PS/2 Y 轴向上为正,翻转为屏幕坐标 */
    *btns = flags & 0x07;

    return 0;
}
