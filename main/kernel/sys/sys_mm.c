/**
 * @file kernel/sys/sys_mm.c
 * @brief 内存管理系统调用
 */

#include <kernel/process/process.h>
#include <kernel/sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/syscall.h>
#include <xnix/vmm.h>

extern void *vmm_kmap(paddr_t paddr);
extern void  vmm_kunmap(void *vaddr);

/**
 * SYS_SBRK: 调整堆大小
 *
 * @param args[0] increment 增量(可以为负数)
 * @return 旧堆顶地址,失败返回 -1
 */
static int32_t sys_sbrk(const uint32_t *args) {
    int32_t increment = (int32_t)args[0];

    struct process *proc = process_get_current();
    if (!proc) {
        return -1;
    }

    uint32_t old_brk = proc->heap_current;

    /* increment == 0:仅返回当前堆顶 */
    if (increment == 0) {
        return (int32_t)old_brk;
    }

    uint32_t new_brk;
    if (increment > 0) {
        new_brk = old_brk + (uint32_t)increment;
        /* 溢出检查 */
        if (new_brk < old_brk) {
            return -1;
        }
        /* 超出堆上限 */
        if (new_brk > proc->heap_max) {
            return -1;
        }
    } else {
        uint32_t dec = (uint32_t)(-increment);
        if (dec > old_brk - proc->heap_start) {
            /* 不能收缩到 heap_start 以下 */
            return -1;
        }
        new_brk = old_brk - dec;
    }

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->map) {
        return -1;
    }

    if (new_brk > old_brk) {
        /* 扩展堆:分配新页面 */
        uint32_t old_page = PAGE_ALIGN_UP(old_brk);
        uint32_t new_page = PAGE_ALIGN_UP(new_brk);

        for (uint32_t vaddr = old_page; vaddr < new_page; vaddr += PAGE_SIZE) {
            /* 检查是否已映射 */
            paddr_t exist = (paddr_t)mm->query(proc->page_dir_phys, vaddr);
            if (exist) {
                continue;
            }

            void *page = alloc_page_high();
            if (!page) {
                /* 分配失败,回滚 */
                proc->heap_current = old_brk;
                return -1;
            }

            if (mm->map(proc->page_dir_phys, vaddr, (paddr_t)page,
                        VMM_PROT_USER | VMM_PROT_READ | VMM_PROT_WRITE) != 0) {
                free_page(page);
                proc->heap_current = old_brk;
                return -1;
            }

            /* 清零新页面 */
            void *k = vmm_kmap((paddr_t)page);
            memset(k, 0, PAGE_SIZE);
            vmm_kunmap(k);
        }
    } else if (new_brk < old_brk && mm->unmap) {
        /* 收缩堆:释放页面 */
        uint32_t old_page = PAGE_ALIGN_UP(old_brk);
        uint32_t new_page = PAGE_ALIGN_UP(new_brk);

        for (uint32_t vaddr = new_page; vaddr < old_page; vaddr += PAGE_SIZE) {
            paddr_t paddr = (paddr_t)mm->query(proc->page_dir_phys, vaddr);
            if (paddr) {
                mm->unmap(proc->page_dir_phys, vaddr);
                free_page((void *)(paddr & PAGE_MASK));
            }
        }
    }

    proc->heap_current = new_brk;
    return (int32_t)old_brk;
}

/**
 * 注册内存管理系统调用
 */
void sys_mm_init(void) {
    syscall_register(SYS_SBRK, sys_sbrk, 1, "sbrk");
}
