#ifndef XNIX_SYSCALL_H
#define XNIX_SYSCALL_H

/**
 * 系统调用号定义
 *
 * 约定:
 * - x86: eax 为 syscall number
 * - 返回值通过 eax 返回
 */
#define SYS_PUTC            1
#define SYS_EXIT            2
#define SYS_ENDPOINT_CREATE 3
#define SYS_IPC_SEND        4
#define SYS_IPC_RECV        5
#define SYS_IPC_CALL        6
#define SYS_IPC_REPLY       7

/**
 * @brief 端口输出 1 字节(受 capability 限制)
 *
 * 参数约定(x86 int 0x80):\n
 * - ebx: ioport capability handle(CAP_TYPE_IOPORT,且具备 CAP_WRITE)\n
 * - ecx: port(uint16_t)\n
 * - edx: value(uint8_t)\n
 *
 * 返回值:\n
 * - 0 成功\n
 * - 负数失败(如 -EPERM:无权限或端口不在授权范围内)
 */
#define SYS_IOPORT_OUTB 8

/**
 * @brief 端口读取 1 字节(受 capability 限制)
 *
 * 参数约定(x86 int 0x80):\n
 * - ebx: ioport capability handle(CAP_TYPE_IOPORT,且具备 CAP_READ)\n
 * - ecx: port(uint16_t)\n
 *
 * 返回值:\n
 * - >=0 读取到的字节(低 8 位有效)\n
 * - 负数失败(如 -EPERM:无权限或端口不在授权范围内)
 */
#define SYS_IOPORT_INB 9

#endif
