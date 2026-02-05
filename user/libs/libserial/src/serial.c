/**
 * @file serial.c
 * @brief Serial driver client SDK implementation
 */

#include <ipc/client.h>
#include <libs/serial/serial.h>
#include <string.h>

static handle_t g_serial_ep = HANDLE_INVALID;

int serial_init(void) {
    g_serial_ep = ipc_ep_find("serial");
    return (g_serial_ep == HANDLE_INVALID) ? -1 : 0;
}

int serial_write(const char *buf, size_t len) {
    if (g_serial_ep == HANDLE_INVALID) {
        serial_init();
        if (g_serial_ep == HANDLE_INVALID) {
            return -1; /* seriald 尚未就绪 */
        }
    }

    if (len == 0) {
        return 0;
    }

    /* 使用 Builder 模式发送数据 */
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = len - sent;
        /* 最大可通过寄存器传输的字节数 */
        size_t max_chunk = ABI_IPC_MSG_PAYLOAD_BYTES;
        if (chunk > max_chunk) {
            chunk = max_chunk;
        }

        struct ipc_builder builder;
        ipc_builder_init(&builder, SERIAL_MSG_WRITE);

        /* 将数据复制到消息寄存器中 */
        memcpy(&builder.msg.regs.data[1], buf + sent, chunk);
        /* 确保 payload 在有效数据之后有 null 终止符 */
        if (chunk < max_chunk) {
            ((char *)&builder.msg.regs.data[1])[chunk] = '\0';
        }

        int ret = ipc_builder_send(&builder, g_serial_ep, 0);
        if (ret < 0) {
            return ret;
        }

        sent += chunk;
    }

    return 0;
}

int serial_putchar(char c) {
    return serial_write(&c, 1);
}

void serial_set_color(uint8_t color) {
    if (g_serial_ep == HANDLE_INVALID) {
        serial_init();
        if (g_serial_ep == HANDLE_INVALID) {
            return;
        }
    }

    ipc_send_simple(g_serial_ep, SERIAL_MSG_COLOR, color, 0);
}
