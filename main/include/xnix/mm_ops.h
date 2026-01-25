#ifndef XNIX_MM_OPS_H
#define XNIX_MM_OPS_H

#include <xnix/types.h>

/**
 * @brief 内存管理操作接口
 * 该接口抽象了底层的内存管理机制,使得内核可以在
 * VMM (基于页表,支持虚拟内存) 和 No-MMU (基于 MPU 或直接映射) 模式之间动态切换.
 */
struct mm_operations {
    const char *name;

    /**
     * @brief 初始化内存管理硬件 (如开启分页,配置 MPU)
     */
    void (*init)(void);

    /**
     * @brief 创建新的地址空间 (Page Directory / MPU Region Set)
     * @return 地址空间的句柄 (物理地址或 ID)
     */
    void *(*create_as)(void);

    /**
     * @brief 销毁地址空间
     */
    void (*destroy_as)(void *as);

    /**
     * @brief 切换当前地址空间
     * @param as 地址空间句柄
     */
    void (*switch_as)(void *as);

    /**
     * @brief 映射页面/区域
     *
     * @param as 地址空间句柄 (NULL 表示当前/内核空间)
     * @param vaddr 虚拟地址
     * @param paddr 物理地址
     * @param flags 权限标志 (VMM_PROT_*)
     * @return 0 成功, 非 0 失败
     */
    int (*map)(void *as, uintptr_t vaddr, uintptr_t paddr, uint32_t flags);

    /**
     * @brief 取消映射
     */
    void (*unmap)(void *as, uintptr_t vaddr);

    /**
     * @brief 查询虚拟地址对应的物理地址
     * @return 物理地址,如果未映射则返回 0 (或特定错误码)
     */
    uintptr_t (*query)(void *as, uintptr_t vaddr);
};

/**
 * @brief 获取当前的内存操作集
 */
const struct mm_operations *mm_get_ops(void);

/**
 * @brief 注册内存操作集 (在启动探测阶段调用)
 */
void mm_register_ops(const struct mm_operations *ops);

#endif /* XNIX_MM_OPS_H */
