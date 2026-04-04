/**
 * @file cap.h
 * @brief 内核能力检查接口
 *
 * 提供 O(1) 位图检查, 替代旧的字符串权限系统.
 */

#ifndef XNIX_CAP_H
#define XNIX_CAP_H

#include <xnix/abi/cap.h>
#include <xnix/types.h>

struct process;

/** 检查进程是否拥有指定能力 */
bool cap_check(struct process *proc, uint32_t cap);

/** 检查进程是否可访问指定 IO 端口 */
bool cap_check_ioport(struct process *proc, uint16_t port);

/** 检查进程是否可注册指定 IRQ */
bool cap_check_irq(struct process *proc, uint8_t irq);

/** 检查 child_caps 是否为 parent_caps 的子集 */
bool cap_is_subset(uint32_t child_caps, uint32_t parent_caps);

/** 从 spawn_caps 构建 ioport_bitmap, 返回分配的位图指针(调用者负责释放) */
uint8_t *cap_build_ioport_bitmap(const struct spawn_caps *caps);

/** 从 spawn_caps 构建 irq_mask */
uint32_t cap_build_irq_mask(const struct spawn_caps *caps);

#endif /* XNIX_CAP_H */
