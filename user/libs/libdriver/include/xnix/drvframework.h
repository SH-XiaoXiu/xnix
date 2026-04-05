/**
 * @file xnix/drvframework.h
 * @brief 驱动框架入口
 *
 * 驱动开发者通过 chardev_register / inputdev_register / displaydev_register
 * 注册设备, 框架自动创建 endpoint、启动服务线程、处理 IPC 协议.
 * 最后调用 driver_run() 阻塞主线程.
 */

#ifndef XNIX_DRVFRAMEWORK_H
#define XNIX_DRVFRAMEWORK_H

/**
 * 阻塞主线程, 等待所有设备线程运行.
 *
 * 调用顺序:
 *   1. 探测硬件
 *   2. xxx_register() 注册各设备实例
 *   3. driver_run()  (不返回)
 */
void driver_run(void);

#endif /* XNIX_DRVFRAMEWORK_H */
