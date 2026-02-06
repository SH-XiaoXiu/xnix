/**
 * @file physmem.h
 * @brief 物理内存区域 (HANDLE_PHYSMEM) 接口
 *
 * 提供物理内存区域对象的创建,引用计数和映射功能.
 * 用于将物理设备内存(如 framebuffer)安全地暴露给用户态.
 */

#ifndef XNIX_PHYSMEM_H
#define XNIX_PHYSMEM_H

#include <arch/mmu.h>

#include <xnix/abi/handle.h>
#include <xnix/types.h>

struct process;

/**
 * Physmem 区域类型
 */
typedef enum {
    PHYSMEM_TYPE_GENERIC = 0, /* 通用物理内存 */
    PHYSMEM_TYPE_FB      = 1, /* Framebuffer */
} physmem_type_t;

/**
 * 物理内存区域对象
 *
 * 表示一段可映射的物理内存区域.
 */
struct physmem_region {
    paddr_t        phys_addr; /* 物理起始地址 */
    uint32_t       size;      /* 区域大小(字节) */
    physmem_type_t type;      /* 区域类型 */
    uint32_t       refcount;  /* 引用计数 */

    /* Framebuffer 元数据(type == PHYSMEM_TYPE_FB 时有效) */
    struct {
        uint32_t width;
        uint32_t height;
        uint32_t pitch;
        uint8_t  bpp;
        uint8_t  red_pos, red_size;
        uint8_t  green_pos, green_size;
        uint8_t  blue_pos, blue_size;
    } fb_info;
};

/**
 * 创建 physmem 区域对象
 *
 * @param phys_addr 物理起始地址
 * @param size      区域大小
 * @param type      区域类型
 * @return physmem 对象指针,失败返回 NULL
 */
struct physmem_region *physmem_create(paddr_t phys_addr, uint32_t size, physmem_type_t type);

/**
 * 增加引用计数
 */
void physmem_get(struct physmem_region *region);

/**
 * 减少引用计数,引用为0时释放
 */
void physmem_put(struct physmem_region *region);

/**
 * 为进程创建 physmem handle
 *
 * 在目标进程的 handle 表中创建一个 HANDLE_PHYSMEM 类型的 handle.
 *
 * @param proc      目标进程
 * @param phys_addr 物理地址
 * @param size      区域大小
 * @param name      handle 名称
 * @return handle 值,失败返回 HANDLE_INVALID
 */
handle_t physmem_create_handle_for_proc(struct process *proc, paddr_t phys_addr, uint32_t size,
                                        const char *name);

/**
 * 创建 framebuffer physmem handle
 *
 * 使用 boot 阶段获取的 framebuffer 信息创建 physmem handle.
 *
 * @param proc 目标进程
 * @param name handle 名称
 * @return handle 值,失败返回 HANDLE_INVALID
 */
handle_t physmem_create_fb_handle_for_proc(struct process *proc, const char *name);

/**
 * 映射 physmem 到用户空间
 *
 * @param proc   目标进程
 * @param region physmem 区域
 * @param offset 区域内偏移
 * @param size   映射大小(0 表示整个区域)
 * @param prot   保护标志
 * @return 用户空间虚拟地址,失败返回 0
 */
uint32_t physmem_map_to_user(struct process *proc, struct physmem_region *region, uint32_t offset,
                             uint32_t size, uint32_t prot);

#endif /* XNIX_PHYSMEM_H */
