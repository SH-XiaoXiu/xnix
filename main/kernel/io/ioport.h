#ifndef KERNEL_IO_IOPORT_H
#define KERNEL_IO_IOPORT_H

#include <xnix/capability.h>
#include <xnix/types.h>

struct process;

/**
 * @brief I/O 端口访问能力对象
 *
 * Xnix 将 x86 的 in/out 指令访问抽象为 capability:
 * - 内核侧通过 SYS_IOPORT_INB/SYS_IOPORT_OUTB 检查调用进程是否持有 CAP_TYPE_IOPORT;
 * - capability 对象表达允许访问的端口范围 [start, end];
 * - rights 用于区分读写:读取需要 CAP_READ,写入需要 CAP_WRITE.
 *
 * - 这是"端口范围"的授权,不做设备级仲裁;设备驱动应在更高层实现互斥/协议.
 * - 对象以 refcount 管理生命周期,cap_table 释放时自动 unref.
 */
struct ioport_range {
    uint16_t start;
    uint16_t end;
    uint32_t refcount;
};

/**
 * @brief I/O 端口能力对象引用计数 +1
 *
 * 供 capability 子系统在复制/持有对象时调用.
 */
void ioport_ref(void *ptr);

/**
 * @brief I/O 端口能力对象引用计数 -1,为 0 时释放对象
 *
 * 供 capability 子系统在释放句柄时调用.
 */
void ioport_unref(void *ptr);

/**
 * @brief 注册 CAP_TYPE_IOPORT 到 capability 类型表
 *
 * 必须在任何 ioport capability 分配/查找之前调用.
 */
void ioport_init(void);

/**
 * @brief 为指定 owner 分配一个 I/O 端口范围 capability
 *
 * @param owner  新 capability 所属进程(句柄将写入其 cap_table)
 * @param start  起始端口(包含)
 * @param end    结束端口(包含)
 * @param rights capability 权限位(至少包含 CAP_READ 或 CAP_WRITE)
 * @return capability 句柄;失败返回 CAP_HANDLE_INVALID
 */
cap_handle_t ioport_create_range(struct process *owner, uint16_t start, uint16_t end,
                                 cap_rights_t rights);

/**
 * @brief 判断端口是否落在授权范围内
 *
 * @param r    ioport_range 对象
 * @param port 目标端口
 * @return true 表示允许访问
 */
bool ioport_range_contains(const struct ioport_range *r, uint16_t port);

#endif
