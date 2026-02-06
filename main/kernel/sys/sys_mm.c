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
 * @param args[0] handle   HANDLE_PHYSMEM 类型的 handle
 * @param args[1] offset   资源内的偏移
 * @param args[2] size     映射大小 (0 = 整个区域)
 * @param args[3] prot     保护标志 (PROT_READ | PROT_WRITE)
 * @param args[4] out_size 可选输出参数: 实际映射的大小(用户空间指针)
 * @return 用户空间虚拟地址, 失败返回负错误码
 *
 * 权限检查: 需要 xnix.mm.mmap 权限
 */
static int32_t sys_mmap_phys(const uint32_t *args) {
    handle_t  handle   = (handle_t)args[0];
    uint32_t  offset   = args[1];
    uint32_t  size     = args[2];
    uint32_t  prot     = args[3];
    uint32_t *out_size = (uint32_t *)(uintptr_t)args[4];

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

    /* 计算实际映射大小 */
    uint32_t actual_size = size;
    if (actual_size == 0 || offset + actual_size > region->size) {
        actual_size = region->size - offset;
    }

    /* 映射到用户空间 */
    uint32_t user_addr = physmem_map_to_user(proc, region, offset, size, prot);
    if (user_addr == 0) {
        return -ENOMEM;
    }

    /* 写入实际大小(如果提供了输出参数) */
    if (out_size) {
        /* TODO: 应验证用户空间指针 */
        *out_size = actual_size;
    }

    return (int32_t)user_addr;
}

/**
 * SYS_PHYSMEM_INFO: 查询物理内存区域信息
 *
 * @param args[0] handle    HANDLE_PHYSMEM 类型的 handle
 * @param args[1] info_ptr  用户空间信息结构指针
 * @return 0 成功, 负数失败
 *
 * 信息结构布局(32字节):
 * - [0-3]   size      区域大小
 * - [4-7]   type      区域类型 (0=generic, 1=fb)
 * - [8-11]  width     FB 宽度(仅 type=1)
 * - [12-15] height    FB 高度(仅 type=1)
 * - [16-19] pitch     FB pitch(仅 type=1)
 * - [20]    bpp       FB bpp(仅 type=1)
 * - [21]    red_pos   (仅 type=1)
 * - [22]    red_size  (仅 type=1)
 * - [23]    green_pos (仅 type=1)
 * - [24]    green_size(仅 type=1)
 * - [25]    blue_pos  (仅 type=1)
 * - [26]    blue_size (仅 type=1)
 * - [27-31] reserved
 */
static int32_t sys_physmem_info(const uint32_t *args) {
    handle_t  handle   = (handle_t)args[0];
    uint8_t  *info_ptr = (uint8_t *)(uintptr_t)args[1];

    struct process *proc = process_get_current();
    if (!proc || !info_ptr) {
        return -EINVAL;
    }

    struct physmem_region *region =
        (struct physmem_region *)handle_resolve(proc, handle, HANDLE_PHYSMEM, PERM_ID_INVALID);
    if (!region) {
        return -EINVAL;
    }

    /* TODO: 验证用户空间指针 */

    /* 写入信息 */
    memset(info_ptr, 0, 32);
    *(uint32_t *)(info_ptr + 0) = region->size;
    *(uint32_t *)(info_ptr + 4) = (uint32_t)region->type;

    if (region->type == PHYSMEM_TYPE_FB) {
        *(uint32_t *)(info_ptr + 8)  = region->fb_info.width;
        *(uint32_t *)(info_ptr + 12) = region->fb_info.height;
        *(uint32_t *)(info_ptr + 16) = region->fb_info.pitch;
        info_ptr[20]                  = region->fb_info.bpp;
        info_ptr[21]                  = region->fb_info.red_pos;
        info_ptr[22]                  = region->fb_info.red_size;
        info_ptr[23]                  = region->fb_info.green_pos;
        info_ptr[24]                  = region->fb_info.green_size;
        info_ptr[25]                  = region->fb_info.blue_pos;
        info_ptr[26]                  = region->fb_info.blue_size;
    }

    return 0;
}

/**
 * 注册内存管理系统调用(编号:200-219)
 */
void sys_mm_init(void) {
    syscall_register(SYS_SBRK, sys_sbrk, 1, "sbrk");
    syscall_register(SYS_MMAP_PHYS, sys_mmap_phys, 5, "mmap_phys");
    syscall_register(SYS_PHYSMEM_INFO, sys_physmem_info, 2, "physmem_info");
}
