/**
 * @file driver_internal.h
 * @brief libdriver 内部共享声明
 */

#ifndef DRIVER_INTERNAL_H
#define DRIVER_INTERNAL_H

#include <errno.h>

/** 通知 driver core 新增了一个设备线程 */
void driver_add_device(void);

#endif /* DRIVER_INTERNAL_H */
