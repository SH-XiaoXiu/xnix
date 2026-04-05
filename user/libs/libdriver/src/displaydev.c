/**
 * @file displaydev.c
 * @brief 显示设备 IPC 协议处理
 */

#include <xnix/displaydev.h>
#include <xnix/protocol/displaydev.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

#include "driver_internal.h"

static int displaydev_handle_msg(struct display_device *dev, struct ipc_message *msg) {
    uint32_t op = msg->regs.data[0];

    switch (op) {
    case DISPDEV_WRITE: {
        if (!dev->ops->write) {
            msg->regs.data[0] = (uint32_t)-ENOSYS;
            return 0;
        }

        uint32_t size = msg->regs.data[1];
        void    *data = (void *)(uintptr_t)msg->buffer.data;
        if (!data || size == 0) {
            msg->regs.data[0] = 0;
            return 0;
        }

        int n = dev->ops->write(dev, data, size);
        msg->regs.data[0] = (uint32_t)n;
        msg->buffer.data = 0;
        msg->buffer.size = 0;
        return 0;
    }

    case DISPDEV_CLEAR: {
        int ret = dev->ops->clear ? dev->ops->clear(dev) : -ENOSYS;
        msg->regs.data[0] = (uint32_t)ret;
        return 0;
    }

    case DISPDEV_SCROLL: {
        int lines = (int)msg->regs.data[1];
        int ret = dev->ops->scroll ? dev->ops->scroll(dev, lines) : -ENOSYS;
        msg->regs.data[0] = (uint32_t)ret;
        return 0;
    }

    case DISPDEV_SET_CURSOR: {
        int row = (int)msg->regs.data[1];
        int col = (int)msg->regs.data[2];
        int ret = dev->ops->set_cursor ? dev->ops->set_cursor(dev, row, col) : -ENOSYS;
        msg->regs.data[0] = (uint32_t)ret;
        return 0;
    }

    case DISPDEV_SET_COLOR: {
        uint8_t attr = (uint8_t)(msg->regs.data[1] & 0xFF);
        int ret = dev->ops->set_color ? dev->ops->set_color(dev, attr) : -ENOSYS;
        msg->regs.data[0] = (uint32_t)ret;
        return 0;
    }

    case DISPDEV_RESET_COLOR: {
        int ret = dev->ops->reset_color ? dev->ops->reset_color(dev) : -ENOSYS;
        msg->regs.data[0] = (uint32_t)ret;
        return 0;
    }

    case DISPDEV_INFO: {
        msg->regs.data[0] = 0;
        msg->regs.data[1] = dev->caps;
        msg->regs.data[2] = (uint32_t)dev->rows;
        msg->regs.data[3] = (uint32_t)dev->cols;
        msg->regs.data[4] = (uint32_t)dev->instance;
        if (dev->name) {
            msg->buffer.data = (uint64_t)(uintptr_t)dev->name;
            msg->buffer.size = (uint32_t)strlen(dev->name) + 1;
        }
        return 0;
    }

    default:
        msg->regs.data[0] = (uint32_t)-ENOSYS;
        return 0;
    }
}

static void *displaydev_thread(void *arg) {
    struct display_device *dev = (struct display_device *)arg;
    char recv_buf[DISPDEV_WRITE_MAX];

    while (1) {
        struct ipc_message msg = {0};
        msg.buffer.data = (uint64_t)(uintptr_t)recv_buf;
        msg.buffer.size = sizeof(recv_buf);

        if (sys_ipc_receive(dev->endpoint, &msg, 0) < 0) {
            continue;
        }

        int ret = displaydev_handle_msg(dev, &msg);
        if (ret == 0) {
            sys_ipc_reply(&msg);
        }
    }
    return NULL;
}

int displaydev_register(struct display_device *dev) {
    if (!dev || !dev->ops || !dev->name) {
        return -1;
    }

    char ep_name[32];
    snprintf(ep_name, sizeof(ep_name), "%s%d", dev->name, dev->instance);

    handle_t ep = env_get_handle(ep_name);
    if (ep == HANDLE_INVALID) {
        ep = env_get_handle(dev->name);
    }
    if (ep == HANDLE_INVALID) {
        int ret = sys_endpoint_create(ep_name);
        if (ret < 0) {
            return -1;
        }
        ep = (handle_t)ret;
    }
    dev->endpoint = ep;

    driver_add_device();

    pthread_t tid;
    if (pthread_create(&tid, NULL, displaydev_thread, dev) != 0) {
        return -1;
    }
    pthread_detach(tid);

    return 0;
}
