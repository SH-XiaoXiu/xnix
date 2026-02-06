/**
 * @file kernel/sys/sys_mm.c
 * @brief 内存管理系统调用
 */

#include <kernel/process/process.h>
#include <kernel/sys/syscall.h>
#include <xnix/errno.h>
#include <xnix/handle.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/perm.h>
#include <xnix/physmem.h>
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
 * SYS_MMAP_PHYS: 映射物理内存到用户空间(使用 handle)
 *
 * @param args[0] handle  HANDLE_PHYSMEM 类型的 handle
 * @param args[1] offset  资源内的偏移
 * @param args[2] size    映射大小
 * @param args[3] prot    保护标志 (PROT_READ | PROT_WRITE)
 * @return 用户空间虚拟地址, 失败返回负错误码
 *
 * 权限检查: 需要 xnix.mm.mmap 权限
 */
static int32_t sys_mmap_phys(const uint32_t *args) {
    handle_t handle = (handle_t)args[0];
    uint32_t offset = args[1];
    uint32_t size   = args[2];
    uint32_t prot   = args[3];

    struct process *proc = process_get_current();
    if (!proc) {
        return -EINVAL;
    }

    /* 解析 handle,验证类型为 HANDLE_PHYSMEM */
    struct physmem_region *region =
        (struct physmem_region *)handle_resolve(proc, handle, HANDLE_PHYSMEM, PERM_ID_INVALID);
    if (!region) {
        pr_warn("sys_mmap_phys: invalid handle %u", handle);
        return -EINVAL;
    }

    /* 映射到用户空间 */
    uint32_t user_addr = physmem_map_to_user(proc, region, offset, size, prot);
    if (user_addr == 0) {
        return -ENOMEM;
    }

    return (int32_t)user_addr;
}

/**
 * 注册内存管理系统调用(编号:200-219)
 */
void sys_mm_init(void) {
    syscall_register(SYS_SBRK, sys_sbrk, 1, "sbrk");
    syscall_register(SYS_MMAP_PHYS, sys_mmap_phys, 4, "mmap_phys");
}
