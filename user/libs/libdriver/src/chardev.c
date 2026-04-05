/**
 * @file chardev.c
 * @brief 字符设备 IPC 协议处理
 *
 * 为每个注册的 char_device 启动一个服务线程,
 * 线程内 receive → 协议解析 → ops 回调 → reply.
 */

#include <xnix/chardev.h>
#include <xnix/protocol/chardev.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

#include "driver_internal.h"

static int chardev_handle_msg(struct char_device *dev, struct ipc_message *msg) {
    uint32_t op = msg->regs.data[0];

    switch (op) {
    case CHARDEV_READ: {
        if (!(dev->caps & CHARDEV_CAP_READ) || !dev->ops->read) {
            msg->regs.data[0] = (uint32_t)-ENOSYS;
            return 0;
        }

        uint32_t max_size = msg->regs.data[1];
        if (max_size == 0) {
            max_size = 1;
        }

        char buf[CHARDEV_IO_MAX];
        if (max_size > CHARDEV_IO_MAX) {
            max_size = CHARDEV_IO_MAX;
        }

        int n = dev->ops->read(dev, buf, max_size);
        if (n < 0) {
            msg->regs.data[0] = (uint32_t)n;
        } else {
            msg->regs.data[0] = (uint32_t)n;
            if (n > 0) {
                msg->buffer.data = (uint64_t)(uintptr_t)buf;
                msg->buffer.size = (uint32_t)n;
            }
        }
        return 0;
    }

    case CHARDEV_WRITE: {
        if (!(dev->caps & CHARDEV_CAP_WRITE) || !dev->ops->write) {
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

    case CHARDEV_IOCTL: {
        if (!dev->ops->ioctl) {
            msg->regs.data[0] = (uint32_t)-ENOSYS;
            return 0;
        }

        uint32_t cmd = msg->regs.data[1];
        int ret = dev->ops->ioctl(dev, cmd, &msg->regs.data[2]);
        msg->regs.data[0] = (uint32_t)ret;
        return 0;
    }

    case CHARDEV_INFO: {
        msg->regs.data[0] = 0;
        msg->regs.data[1] = dev->caps;
        msg->regs.data[2] = (uint32_t)dev->instance;
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

static void *chardev_thread(void *arg) {
    struct char_device *dev = (struct char_device *)arg;
    char recv_buf[CHARDEV_IO_MAX];

    while (1) {
        struct ipc_message msg = {0};
        msg.buffer.data = (uint64_t)(uintptr_t)recv_buf;
        msg.buffer.size = sizeof(recv_buf);

        if (sys_ipc_receive(dev->endpoint, &msg, 0) < 0) {
            continue;
        }

        chardev_handle_msg(dev, &msg);
        sys_ipc_reply(&msg);
    }
    return NULL;
}

int chardev_register(struct char_device *dev) {
    if (!dev || !dev->ops || !dev->name) {
        return -1;
    }

    /* 如果驱动已预设 endpoint (如 init 注入), 直接使用 */
    if (dev->endpoint == HANDLE_INVALID) {
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
    }

    driver_add_device();

    pthread_t tid;
    if (pthread_create(&tid, NULL, chardev_thread, dev) != 0) {
        return -1;
    }
    pthread_detach(tid);

    return 0;
}
