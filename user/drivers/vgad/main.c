#include "vga.h"

#include <d/protocol/serial.h>
#include <d/server.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xnix/abi/handle.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

static struct vga_state g_vga;
static pthread_mutex_t  g_vga_lock;

static int console_handler(struct ipc_message *msg) {
    uint32_t opcode = msg->regs.data[0];

    switch (opcode) {
    case UDM_CONSOLE_PUTC: {
        char c = (char)(msg->regs.data[1] & 0xFF);
        pthread_mutex_lock(&g_vga_lock);
        vga_putc(&g_vga, c);
        pthread_mutex_unlock(&g_vga_lock);
        break;
    }
    case UDM_CONSOLE_WRITE: {
        const char *data = (const char *)&msg->regs.data[1];
        size_t      len  = (size_t)(msg->regs.data[7] & 0xFF);
        if (len > 24) {
            len = 24; /* UDM_CONSOLE_WRITE_MAX */
        }
        pthread_mutex_lock(&g_vga_lock);
        vga_write(&g_vga, data, len);
        pthread_mutex_unlock(&g_vga_lock);
        break;
    }
    case UDM_CONSOLE_SET_COLOR: {
        uint8_t attr = (uint8_t)(msg->regs.data[1] & 0xFF);
        pthread_mutex_lock(&g_vga_lock);
        vga_set_color(&g_vga, attr & 0x0F, (attr >> 4) & 0x0F);
        pthread_mutex_unlock(&g_vga_lock);
        break;
    }
    case UDM_CONSOLE_RESET_COLOR:
        pthread_mutex_lock(&g_vga_lock);
        vga_reset_color(&g_vga);
        pthread_mutex_unlock(&g_vga_lock);
        break;
    case UDM_CONSOLE_CLEAR:
        pthread_mutex_lock(&g_vga_lock);
        vga_clear(&g_vga);
        pthread_mutex_unlock(&g_vga_lock);
        break;
    }
    return 0;
}

int main(void) {
    /* Get vga_mem physmem handle (0xB8000) */
    handle_t vga_mem = env_get_handle("vga_mem");
    if (vga_mem == HANDLE_INVALID) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[vgad]", " ERROR: vga_mem handle not found\n");
        return 1;
    }

    /* 映射VGA缓冲区 */
    void *addr = sys_mmap_phys(vga_mem, 0, 0x1000, 0x03, NULL);
    if (!addr || (intptr_t)addr < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[vgad]", " ERROR: failed to map VGA buffer (%d)\n",
                  (int)(intptr_t)addr);
        return 1;
    }

    vga_state_init(&g_vga);
    g_vga.buffer = (uint16_t *)addr;
    pthread_mutex_init(&g_vga_lock, NULL);
    vga_hw_init();
    vga_clear(&g_vga);

    /* 获取 vga_ep 端点 - 应该由init通过provides传递 */
    handle_t ep = env_get_handle("vga_ep");
    if (ep == HANDLE_INVALID) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[vgad]", " ERROR: vga_ep handle not found\n");
        return 1;
    }

    struct udm_server srv = {
        .endpoint = ep,
        .handler  = console_handler,
        .name     = "vgad",
    };

    udm_server_init(&srv);
    svc_notify_ready("vgad");
    udm_server_run(&srv);
    return 0;
}
