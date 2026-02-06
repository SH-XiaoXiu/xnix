#ifndef XNIX_VMM_H
#define XNIX_VMM_H

#include <arch/mmu.h>

#include <xnix/types.h>

/* VMM 标志位 */
#define VMM_PROT_READ    (1 << 0)
#define VMM_PROT_WRITE   (1 << 1)
#define VMM_PROT_USER    (1 << 2) /* 用户态可访问 */
#define VMM_PROT_NOCACHE (1 << 3) /* 不可缓存 (用于 MMIO) */
#define VMM_PROT_NONE    0

/* 架构无关的 VMM 接口 */

/* 初始化 VMM (启用分页, 创建内核页表) */
void vmm_init(void);

/* 创建一个新的页目录 (用于新进程) */
/* 返回物理地址 (用于 CR3) */
void *vmm_create_pd(void);

/* 销毁页目录 */
void vmm_destroy_pd(void *pd_phys);

/* 切换地址空间 */
void vmm_switch_pd(void *pd_phys);

/* 映射一页 */
/* pd_phys: 页目录物理地址 (如果为 NULL, 使用当前页目录) */
/* 返回 0 成功, <0 失败 */
int vmm_map_page(void *pd_phys, vaddr_t vaddr, paddr_t paddr, uint32_t flags);

/* 取消映射一页 */
void vmm_unmap_page(void *pd_phys, vaddr_t vaddr);

/* 获取虚拟地址对应的物理地址 */
/* 返回 0 表示未映射 */
paddr_t vmm_get_paddr(void *pd_phys, vaddr_t vaddr);

int vmm_query_flags(void *pd_phys, vaddr_t vaddr, paddr_t *out_paddr, uint32_t *out_flags);

/* 缺页异常处理 */
struct irq_regs; /* 前向声明 */
void vmm_page_fault(struct irq_regs *frame, vaddr_t vaddr);

/* 获取内核页目录物理地址 */
void *vmm_get_kernel_pd(void);

#endif
