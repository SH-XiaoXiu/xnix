/**
 * @file main.c
 * @brief Seriald UDM 驱动入口
 */

#include "serial.h"

#include <udm/server.h>
#include <xnix/udm/console.h>

#define BOOT_CONSOLE_EP 0
#define BOOT_IOPORT_CAP 1

static int console_handler(struct ipc_message *msg) {
    uint32_t op = UDM_MSG_OPCODE(msg);

    switch (op) {
    case UDM_CONSOLE_PUTC:
        serial_putc(UDM_MSG_ARG(msg, 0) & 0xFF);
        break;
    case UDM_CONSOLE_WRITE: {
        /* 字符串从 data[1] 开始,最多 24 字节 */
        const char *str = (const char *)&msg->regs.data[1];
        for (uint32_t i = 0; i < UDM_CONSOLE_WRITE_MAX && str[i]; i++) {
            serial_putc(str[i]);
        }
        break;
    }
    case UDM_CONSOLE_SET_COLOR:
        serial_set_color(UDM_MSG_ARG(msg, 0));
        break;
    case UDM_CONSOLE_RESET_COLOR:
        serial_reset_color();
        break;
    case UDM_CONSOLE_CLEAR:
        serial_clear();
        break;
    default:
        break;
    }
    return 0;
}

int main(void) {
    serial_init(BOOT_IOPORT_CAP);

    struct udm_server srv = {
        .endpoint = BOOT_CONSOLE_EP,
        .handler  = console_handler,
        .name     = "seriald",
    };

    udm_server_init(&srv);
    udm_server_run(&srv);

    return 0;
}
