/**
 * @file main.c
 * @brief moused UDM 驱动 - PS/2 鼠标
 *
 * 从 PS/2 读取鼠标数据包,通过 IPC 向客户端提供鼠标输入.
 */

#include "ps2.h"

#include <xnix/protocol/mouse.h>
#include <d/server.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/svc.h>
#include <xnix/syscall.h>
#include <xnix/ulog.h>

#define PACKET_BUF_SIZE 64

/* 鼠标数据包 */
struct mouse_packet {
    int16_t dx;
    int16_t dy;
    uint8_t buttons;
};

/* 环形缓冲区 */
static struct mouse_packet packet_buf[PACKET_BUF_SIZE];
static volatile uint16_t  packet_head = 0;
static volatile uint16_t  packet_tail = 0;
static pthread_mutex_t    packet_lock;

/* 挂起的 READ_PACKET 请求 */
static uint32_t        pending_read_tid = 0;
static pthread_mutex_t pending_lock;

/**
 * 写入鼠标包到缓冲区
 */
static void packet_write(int16_t dx, int16_t dy, uint8_t buttons) {
    pthread_mutex_lock(&packet_lock);

    uint16_t next = (packet_head + 1) % PACKET_BUF_SIZE;
    if (next != packet_tail) {
        packet_buf[packet_head].dx      = dx;
        packet_buf[packet_head].dy      = dy;
        packet_buf[packet_head].buttons = buttons;
        packet_head                     = next;
    }

    pthread_mutex_unlock(&packet_lock);
}

/**
 * 从缓冲区读取鼠标包 (非阻塞)
 * @return 0 成功, -1 无数据
 */
static int packet_read(struct mouse_packet *out) {
    pthread_mutex_lock(&packet_lock);

    if (packet_head == packet_tail) {
        pthread_mutex_unlock(&packet_lock);
        return -1;
    }

    *out         = packet_buf[packet_tail];
    packet_tail = (packet_tail + 1) % PACKET_BUF_SIZE;

    pthread_mutex_unlock(&packet_lock);
    return 0;
}

/**
 * 尝试满足挂起的读请求
 */
static void try_fulfill_pending_read(void) {
    pthread_mutex_lock(&pending_lock);

    if (pending_read_tid == 0) {
        pthread_mutex_unlock(&pending_lock);
        return;
    }

    struct mouse_packet pkt;
    if (packet_read(&pkt) < 0) {
        pthread_mutex_unlock(&pending_lock);
        return;
    }

    struct ipc_message reply = {0};
    reply.regs.data[0] = UDM_OK;
    reply.regs.data[1] = (uint32_t)(uint16_t)pkt.dx;
    reply.regs.data[2] = (uint32_t)(uint16_t)pkt.dy;
    reply.regs.data[3] = pkt.buttons;

    sys_ipc_reply_to(pending_read_tid, &reply);
    pending_read_tid = 0;

    pthread_mutex_unlock(&pending_lock);
}

/**
 * IPC 消息处理
 */
static int mouse_handler(struct ipc_message *msg) {
    uint32_t op = UDM_MSG_OPCODE(msg);

    switch (op) {
    case UDM_MOUSE_READ_PACKET: {
        struct mouse_packet pkt;
        if (packet_read(&pkt) == 0) {
            msg->regs.data[0] = UDM_OK;
            msg->regs.data[1] = (uint32_t)(uint16_t)pkt.dx;
            msg->regs.data[2] = (uint32_t)(uint16_t)pkt.dy;
            msg->regs.data[3] = pkt.buttons;
        } else {
            /* 无数据,延迟回复 */
            pthread_mutex_lock(&pending_lock);
            if (msg->sender_tid == 0) {
                msg->regs.data[0] = UDM_ERR_UNKNOWN;
                pthread_mutex_unlock(&pending_lock);
            } else if (pending_read_tid != 0 &&
                       pending_read_tid != msg->sender_tid) {
                msg->regs.data[0] = UDM_ERR_BUSY;
                pthread_mutex_unlock(&pending_lock);
            } else {
                pending_read_tid = msg->sender_tid;
                pthread_mutex_unlock(&pending_lock);
                return 1; /* 不立即回复 */
            }
        }
        break;
    }

    case UDM_MOUSE_POLL: {
        pthread_mutex_lock(&packet_lock);
        int has_data = (packet_head != packet_tail) ? 1 : 0;
        pthread_mutex_unlock(&packet_lock);
        msg->regs.data[0] = has_data;
        break;
    }

    default:
        msg->regs.data[0] = UDM_ERR_INVALID;
        break;
    }

    return 0;
}

/**
 * 鼠标 IRQ 处理线程
 */
static void *mouse_irq_thread(void *arg) {
    (void)arg;

    /* 初始化 PS/2 鼠标硬件 */
    if (ps2_mouse_init() < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[moused]",
                  " PS/2 mouse init failed\n");
        return NULL;
    }

    /* 绑定 IRQ12 */
    int ret = sys_irq_bind(IRQ_MOUSE, -1, 0);
    if (ret < 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[moused]",
                  " Failed to bind IRQ %d\n", IRQ_MOUSE);
        return NULL;
    }

    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[moused]",
              " IRQ %d bound, reading mouse data\n", IRQ_MOUSE);

    uint8_t  packet[3];
    int      byte_idx = 0;

    while (1) {
        uint8_t byte;
        ret = sys_irq_read(IRQ_MOUSE, &byte, 1, 0);
        if (ret <= 0) {
            continue;
        }

        /* 同步: 第一个字节的 bit 3 必须为 1 */
        if (byte_idx == 0 && !(byte & 0x08)) {
            continue; /* 等待同步 */
        }

        packet[byte_idx++] = byte;

        if (byte_idx == 3) {
            byte_idx = 0;

            int16_t dx, dy;
            uint8_t btns;
            if (ps2_mouse_parse(packet, &dx, &dy, &btns) == 0) {
                packet_write(dx, dy, btns);
                try_fulfill_pending_read();
            }
        }
    }

    return NULL;
}

int main(void) {
    pthread_mutex_init(&packet_lock, NULL);
    pthread_mutex_init(&pending_lock, NULL);

    env_set_name("moused");
    handle_t mouse_ep = env_require("mouse_ep");
    if (mouse_ep == HANDLE_INVALID) {
        return 1;
    }

    /* 启动 IRQ 处理线程 */
    pthread_t irq_tid;
    if (pthread_create(&irq_tid, NULL, mouse_irq_thread, NULL) != 0) {
        ulog_tagf(stdout, TERM_COLOR_LIGHT_RED, "[moused]",
                  " Failed to create IRQ thread\n");
        return 1;
    }
    ulog_tagf(stdout, TERM_COLOR_LIGHT_GREEN, "[moused]", " started\n");

    /* 启动 IPC 服务器 */
    struct udm_server srv = {
        .endpoint = mouse_ep,
        .handler  = mouse_handler,
        .name     = "moused",
    };

    udm_server_init(&srv);
    svc_notify_ready("moused");
    udm_server_run(&srv);

    return 0;
}
