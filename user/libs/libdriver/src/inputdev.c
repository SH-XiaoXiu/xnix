/**
 * @file inputdev.c
 * @brief 输入设备 IPC 协议处理
 */

#include <xnix/inputdev.h>
#include <xnix/protocol/inputdev.h>
#include <xnix/protocol/input.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <xnix/env.h>
#include <xnix/ipc.h>
#include <xnix/syscall.h>

#include "driver_internal.h"

static int inputdev_handle_msg(struct input_device *dev, struct ipc_message *msg) {
    uint32_t op = msg->regs.data[0];

    switch (op) {
    case INPUTDEV_READ: {
        if (!dev->ops->read_event) {
            msg->regs.data[0] = (uint32_t)-ENOSYS;
            return 0;
        }

        struct input_event ev;
        int ret = dev->ops->read_event(dev, &ev);
        if (ret < 0) {
            msg->regs.data[0] = (uint32_t)ret;
        } else {
            msg->regs.data[0] = 0;
            msg->regs.data[1] = INPUT_PACK_REG1(&ev);
            msg->regs.data[2] = INPUT_PACK_REG2(&ev);
            msg->regs.data[3] = INPUT_PACK_REG3(&ev);
        }
        return 0;
    }

    case INPUTDEV_POLL: {
        int count = 0;
        if (dev->ops->poll) {
            count = dev->ops->poll(dev);
        }
        msg->regs.data[0] = (uint32_t)count;
        return 0;
    }

    case INPUTDEV_INFO: {
        msg->regs.data[0] = 0;
        msg->regs.data[1] = dev->caps;
        msg->regs.data[2] = dev->type;
        msg->regs.data[3] = (uint32_t)dev->instance;
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

static void *inputdev_thread(void *arg) {
    struct input_device *dev = (struct input_device *)arg;

    while (1) {
        struct ipc_message msg = {0};

        if (sys_ipc_receive(dev->endpoint, &msg, 0) < 0) {
            continue;
        }

        int ret = inputdev_handle_msg(dev, &msg);
        if (ret == 0) {
            sys_ipc_reply(&msg);
        }
    }
    return NULL;
}

int inputdev_register(struct input_device *dev) {
    if (!dev || !dev->ops || !dev->name) {
        return -1;
    }

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
    if (pthread_create(&tid, NULL, inputdev_thread, dev) != 0) {
        return -1;
    }
    pthread_detach(tid);

    return 0;
}
