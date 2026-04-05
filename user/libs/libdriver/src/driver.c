/**
 * @file driver.c
 * @brief 驱动框架核心
 *
 * 管理已注册设备的计数, 提供 driver_run() 阻塞主线程.
 */

#include <xnix/driver.h>

#include <pthread.h>
#include <unistd.h>

#include "driver_internal.h"

static volatile int g_device_count = 0;
static pthread_mutex_t g_count_lock = 0;

void driver_add_device(void) {
    pthread_mutex_lock(&g_count_lock);
    g_device_count++;
    pthread_mutex_unlock(&g_count_lock);
}

void driver_run(void) {
    /* 所有设备线程已 detach, 主线程只需保持进程存活 */
    while (1) {
        msleep(1000);
    }
}
