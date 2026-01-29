#include <arch/mmu.h>
#include <arch/smp.h>

#include <asm/irq_defs.h>
#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/sync.h>
#include <xnix/vmm.h>

/* x86 页表相关定义 */
#define PTE_PRESENT  0x01
#define PTE_RW       0x02
#define PTE_USER     0x04
#define PTE_PWT      0x08 /* Page Write-Through */
#define PTE_PCD      0x10 /* Page Cache Disable (用于 MMIO) */
#define PTE_ACCESSED 0x20
#define PTE_DIRTY    0x40

#define PDE_PRESENT  0x01
#define PDE_RW       0x02
#define PDE_USER     0x04
#define PDE_ACCESSED 0x20
#define PDE_DIRTY    0x40

/* 递归映射相关 */
#define PD_INDEX(vaddr) ((vaddr) >> 22)
#define PT_INDEX(vaddr) (((vaddr) >> 12) & 0x3FF)

/* 内核页目录 (物理地址) */
static uint32_t *kernel_pd = NULL;

static uint32_t kmap_irq_flags[CFG_MAX_CPUS];

/*
 * 临时映射窗口管理
 *
 * PD[1022] 作为临时映射专用的页目录项.
 *
 * 虚拟地址范围: 0xFF800000 - 0xFFBFFFFF (4MB)
 *
 * 支持多个窗口,通过 spinlock 保护分配.
 * 为了简单,我们目前支持两个窗口,使用简单的位掩码或锁保护.
 *
 * TEMP_WINDOW_1: 0xFFBFF000 (PT[1023]) - 用于映射 PD
 * TEMP_WINDOW_2: 0xFFBFE000 (PT[1022]) - 用于映射 PT
 *
 * 注意:这只是一个简单的实现,如果是真正的多核系统,应该使用 per-CPU 窗口
 * 或者更复杂的分配器.目前我们加全局锁.
 *
 * TODO: 实现 per-CPU 临时映射窗口
 */
#define TEMP_PT_PD_IDX 1022
#define TEMP_WINDOW_1  (0xFF800000 + (1023 * 4096))
#define TEMP_WINDOW_2  (0xFF800000 + (1022 * 4096))

static spinlock_t temp_map_lock = SPINLOCK_INIT;

/* 汇编辅助函数 */
extern void load_cr3(uint32_t cr3);
extern void enable_paging(void);

static inline void invlpg(vaddr_t vaddr) {
    asm volatile("invlpg (%0)" ::"r"(vaddr) : "memory");
}

/* 将通用标志转换为 x86 标志 */
static uint32_t vmm_flags_to_x86(uint32_t flags) {
    uint32_t x86_flags = PTE_PRESENT; /* 总是 Present */
    if (flags & VMM_PROT_WRITE) {
        x86_flags |= PTE_RW;
    }
    if (flags & VMM_PROT_USER) {
        x86_flags |= PTE_USER;
    }
    if (flags & VMM_PROT_NOCACHE) {
        x86_flags |= PTE_PCD | PTE_PWT; /* MMIO 需要禁用缓存 */
    }
    return x86_flags;
}

/*
 * 映射任意物理页到临时窗口
 * window_id: 1 或 2
 * 返回虚拟地址
 *
 * 注意:必须先持有 temp_map_lock!
 */
static void *map_temp_page(int window_id, paddr_t paddr) {
    /*
     * 临时 PT 的虚拟地址
     * PD[1022] -> temp_pt
     * 通过递归映射访问 temp_pt:
     * PD[1023] -> PD
     * 访问 PD[1022] 对应的 PT -> 0xFFC00000 + (1022 * 4096) = 0xFFFFE000
     */
    uint32_t *temp_pt_virt = (uint32_t *)0xFFFFE000;

    uint32_t pt_idx = (window_id == 1) ? 1023 : 1022;
    vaddr_t  vaddr  = (window_id == 1) ? TEMP_WINDOW_1 : TEMP_WINDOW_2;

    temp_pt_virt[pt_idx] = (paddr & PAGE_MASK) | PTE_PRESENT | PTE_RW;
    invlpg(vaddr);

    return (void *)vaddr;
}

static void unmap_temp_page(int window_id) {
    uint32_t *temp_pt_virt = (uint32_t *)0xFFFFE000;
    uint32_t  pt_idx       = (window_id == 1) ? 1023 : 1022;
    vaddr_t   vaddr        = (window_id == 1) ? TEMP_WINDOW_1 : TEMP_WINDOW_2;

    temp_pt_virt[pt_idx] = 0;
    invlpg(vaddr);
}

/*
 * 临时映射 (HighMem access)
 *
 * 借用 TEMP_WINDOW_2 (窗口2) 来映射任意物理页,以便内核访问.
 * 注意:这只是一个简单的实现,每次只能映射一页,且必须在临界区内使用.
 *
 * 使用 irqsave 版本防止中断期间重入.
 */
void *vmm_kmap(paddr_t paddr) {
    cpu_id_t cpu = cpu_current_id();
    if (cpu >= CFG_MAX_CPUS) {
        cpu = 0;
    }
    kmap_irq_flags[cpu] = spin_lock_irqsave(&temp_map_lock);
    return map_temp_page(2, paddr);
}

void vmm_kunmap(void *vaddr) {
    (void)vaddr;
    unmap_temp_page(2);
    cpu_id_t cpu = cpu_current_id();
    if (cpu >= CFG_MAX_CPUS) {
        cpu = 0;
    }
    spin_unlock_irqrestore(&temp_map_lock, kmap_irq_flags[cpu]);
}

void vmm_init(void) {
    /* 分配内核页目录 */
    kernel_pd = (uint32_t *)alloc_page();
    if (!kernel_pd) {
        panic("Failed to allocate kernel page directory");
    }
    memset(kernel_pd, 0, PAGE_SIZE);

    /* 恒等映射：只映射低地址区域，避免与用户空间冲突 */
    paddr_t start, end;
    arch_get_memory_range(&start, &end);

    uint32_t max_idmap = (uint32_t)CFG_KERNEL_IDMAP_MB * 1024u * 1024u;
    if (max_idmap && end > max_idmap) {
        end = max_idmap;
    }
    uint32_t map_end = (end + 0x3FFFFF) & ~0x3FFFFF;

    /*
     * 映射所有探测到的物理内存
     */

    uint32_t pt_count = map_end >> 22;

    if (pt_count > 1024) { /* 最大 4GB */
        pt_count = 1024;
        pr_warn("Physical memory too large, truncating mapping to 4GB");
    }

    for (uint32_t i = 0; i < pt_count; i++) {
        uint32_t *pt = (uint32_t *)alloc_page();
        if (!pt) {
            pr_warn("Partial kernel PT allocated, memory beyond mapped may cause page fault");
            break;
        }

        memset(pt, 0, PAGE_SIZE);

        /* 填充页表项 */
        for (uint32_t j = 0; j < 1024; j++) {
            uint32_t paddr = (i * 1024 * 4096) + (j * 4096);
            if (paddr < end) {
                pt[j] = paddr | PTE_PRESENT | PTE_RW;
            } else {
                pt[j] = 0;
            }
        }
        /* 写入页目录: Present, RW, Supervisor */
        kernel_pd[i] = ((uint32_t)pt) | PDE_PRESENT | PDE_RW;
    }

    /* 分配临时映射专用 PT */
    uint32_t *temp_pt = (uint32_t *)alloc_page();
    if (!temp_pt) {
        panic("Failed to allocate temp PT");
    }
    memset(temp_pt, 0, PAGE_SIZE);

    /* 注册临时 PT 到 PD[1022] */
    kernel_pd[TEMP_PT_PD_IDX] = ((uint32_t)temp_pt) | PDE_PRESENT | PDE_RW;

    /* 递归映射: 最后一个 PDE 指向 PD 自身 */
    kernel_pd[1023] = ((uint32_t)kernel_pd) | PDE_PRESENT | PDE_RW;

    /* 启用分页 */
    load_cr3((uint32_t)kernel_pd);
    enable_paging();
    pr_ok("VMM initialized, Paging enabled, mapped %u MB (IDMAP %u MB)", map_end / 1024 / 1024,
          (uint32_t)CFG_KERNEL_IDMAP_MB);
}

int vmm_map_page(void *pd_phys, vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
    /* 禁止映射 NULL 页面(虚拟地址 0)到用户空间 */
    if (vaddr < PAGE_SIZE && (flags & VMM_PROT_USER)) {
        pr_err("vmm_map_page: attempted to map NULL page (vaddr=0x%x, paddr=0x%x)", vaddr, paddr);
        return -1;
    }

    uint32_t  pd_idx = PD_INDEX(vaddr);
    uint32_t  pt_idx = PT_INDEX(vaddr);
    uint32_t *pd_virt;
    uint32_t *pt_virt;

    /* 判断是否操作当前 PD */
    uint32_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    bool is_current = ((uint32_t)pd_phys == current_cr3) || (pd_phys == NULL);

    /* 如果不是当前 PD,需要加锁使用临时窗口 */
    uint32_t irq_flags = 0;
    if (!is_current) {
        irq_flags = spin_lock_irqsave(&temp_map_lock);
    }

    if (is_current) {
        pd_virt = (uint32_t *)0xFFFFF000;
    } else {
        /* 映射目标 PD 到窗口 1 */
        pd_virt = (uint32_t *)map_temp_page(1, (paddr_t)pd_phys);
    }

    /* 检查 PDE */
    if (!(pd_virt[pd_idx] & PDE_PRESENT)) {
        /* 分配新页表 */
        uint32_t *new_pt_phys = (uint32_t *)alloc_page();
        if (!new_pt_phys) {
            if (!is_current) {
                unmap_temp_page(1);
                spin_unlock_irqrestore(&temp_map_lock, irq_flags);
            }
            return -ENOMEM;
        }

        /* 清零新页表 - 使用窗口 2 */

        uint32_t temp_irq_flags = 0;
        if (is_current) {
            temp_irq_flags = spin_lock_irqsave(&temp_map_lock);
        }

        void *ptr = map_temp_page(2, (paddr_t)new_pt_phys);
        memset(ptr, 0, PAGE_SIZE);
        unmap_temp_page(2);

        if (is_current) {
            spin_unlock_irqrestore(&temp_map_lock, temp_irq_flags);
        }

        /* 设置 PDE 权限: 如果请求是用户态, PDE 也必须是用户态 */
        uint32_t pde_flags = PDE_PRESENT | PDE_RW;
        if (flags & VMM_PROT_USER) {
            pde_flags |= PDE_USER;
        }

        pd_virt[pd_idx] = ((uint32_t)new_pt_phys) | pde_flags;
    } else {
        /* PDE 已存在,确保权限足够 */
        uint32_t need = PDE_PRESENT;
        if (flags & VMM_PROT_USER) {
            need |= PDE_USER;
        }
        if (flags & VMM_PROT_WRITE) {
            need |= PDE_RW;
        }
        if ((pd_virt[pd_idx] & need) != need) {
            pd_virt[pd_idx] |= need;
        }
    }

    /* 获取 PT 物理地址 */
    uint32_t pt_phys = pd_virt[pd_idx] & PAGE_MASK;

    if (is_current) {
        /* 递归映射访问 PT */
        pt_virt = (uint32_t *)(0xFFC00000 + (pd_idx << 12));
    } else {
        /* 映射目标 PT 到窗口 2 */
        pt_virt = (uint32_t *)map_temp_page(2, pt_phys);
    }

    /* 写入 PTE */
    pt_virt[pt_idx] = (paddr & PAGE_MASK) | vmm_flags_to_x86(flags);

    if (is_current) {
        invlpg(vaddr);
    } else {
        unmap_temp_page(2);
        unmap_temp_page(1);
        spin_unlock_irqrestore(&temp_map_lock, irq_flags);
        /* 无法刷新目标地址空间的 TLB,但也没关系,因为它没在运行 */
    }

    return 0;
}

void vmm_unmap_page(void *pd_phys, vaddr_t vaddr) {
    uint32_t  pd_idx = PD_INDEX(vaddr);
    uint32_t  pt_idx = PT_INDEX(vaddr);
    uint32_t *pd_virt;
    uint32_t *pt_virt;

    uint32_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    bool is_current = ((uint32_t)pd_phys == current_cr3) || (pd_phys == NULL);

    uint32_t irq_flags = 0;
    if (!is_current) {
        irq_flags = spin_lock_irqsave(&temp_map_lock);
    }

    if (is_current) {
        pd_virt = (uint32_t *)0xFFFFF000;
    } else {
        pd_virt = (uint32_t *)map_temp_page(1, (paddr_t)pd_phys);
    }

    if (!(pd_virt[pd_idx] & PDE_PRESENT)) {
        if (!is_current) {
            unmap_temp_page(1);
            spin_unlock_irqrestore(&temp_map_lock, irq_flags);
        }
        return;
    }

    uint32_t pt_phys = pd_virt[pd_idx] & PAGE_MASK;

    if (is_current) {
        pt_virt = (uint32_t *)(0xFFC00000 + (pd_idx << 12));
    } else {
        pt_virt = (uint32_t *)map_temp_page(2, pt_phys);
    }

    pt_virt[pt_idx] = 0;

    if (is_current) {
        invlpg(vaddr);
    } else {
        unmap_temp_page(2);
        unmap_temp_page(1);
        spin_unlock_irqrestore(&temp_map_lock, irq_flags);
    }
}

void *vmm_create_pd(void) {
    uint32_t *pd_phys = (uint32_t *)alloc_page();
    if (!pd_phys) {
        return NULL;
    }

    /* 映射新 PD 到窗口 1 进行初始化 */
    uint32_t  irq_flags = spin_lock_irqsave(&temp_map_lock);
    uint32_t *pd_virt   = (uint32_t *)map_temp_page(1, (paddr_t)pd_phys);
    memset(pd_virt, 0, PAGE_SIZE);

    /*
     * 访问 kernel_pd 以拷贝内核映射
     * kernel_pd 是物理地址,需要正确映射才能访问
     */
    uint32_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    bool      kernel_is_current = ((uint32_t)kernel_pd == current_cr3);
    uint32_t *kpd_virt;

    if (kernel_is_current) {
        /* 当前 CR3 就是 kernel_pd,通过递归映射访问 */
        kpd_virt = (uint32_t *)0xFFFFF000;
    } else {
        /* 当前 CR3 是其他页目录,需要映射 kernel_pd 到窗口 2 */
        kpd_virt = (uint32_t *)map_temp_page(2, (paddr_t)kernel_pd);
    }

    /*
     * 拷贝内核映射
     * 内核当前在 1MB，通过恒等映射访问。用户进程陷入内核时（中断/系统调用）
     * 也需要访问内核代码，所以必须拷贝包含内核的 PDE。
     * 但我们只拷贝**真正包含内核代码**的 PDE，而不是整个恒等映射范围。
     * 内核在 1-2MB 范围，只需拷贝 PDE[0] 即可。
     */
    int copied_count = 0;

    /* 拷贝 PDE[0]：包含内核代码（1MB） */
    if (kpd_virt[0] & PDE_PRESENT) {
        pd_virt[0] = kpd_virt[0];
        copied_count++;
    }

    /* 拷贝高地址内核空间（>=3GB） */
    for (int i = 768; i < 1022; i++) {
        if (kpd_virt[i] & PDE_PRESENT) {
            pd_virt[i] = kpd_virt[i];
            copied_count++;
        }
    }

    /* 拷贝 Temp PT 映射 (PD[1022]) 以便新进程也能使用临时映射 */
    pd_virt[TEMP_PT_PD_IDX] = kpd_virt[TEMP_PT_PD_IDX];

    /* 如果映射了 kernel_pd,需要解除映射 */
    if (!kernel_is_current) {
        unmap_temp_page(2);
    }

    /* 递归映射: 最后一项指向自己 */
    pd_virt[1023] = ((uint32_t)pd_phys) | PDE_PRESENT | PDE_RW;

    unmap_temp_page(1);
    spin_unlock_irqrestore(&temp_map_lock, irq_flags);
    return pd_phys;
}

void vmm_destroy_pd(void *pd_phys) {
    if (pd_phys == kernel_pd) {
        return; /* 不能销毁内核 PD */
    }

    uint32_t irq_flags = spin_lock_irqsave(&temp_map_lock);

    /* 映射 PD 到窗口 1 */
    uint32_t *pd_virt = (uint32_t *)map_temp_page(1, (paddr_t)pd_phys);

    /* 遍历 PD,释放用户态的 PT */
    for (int i = 0; i < 1022; i++) {
        if (pd_virt[i] & PDE_PRESENT) {
            /* 只释放用户态页表 */
            if (pd_virt[i] & PDE_USER) {
                paddr_t pt_phys = pd_virt[i] & PAGE_MASK;
                free_page((void *)pt_phys);
            }
        }
    }

    unmap_temp_page(1);
    spin_unlock_irqrestore(&temp_map_lock, irq_flags);

    free_page(pd_phys);
}

paddr_t vmm_get_paddr(void *pd_phys, vaddr_t vaddr) {
    uint32_t  pd_idx = PD_INDEX(vaddr);
    uint32_t  pt_idx = PT_INDEX(vaddr);
    uint32_t *pd_virt;
    uint32_t *pt_virt;
    paddr_t   paddr = 0;

    uint32_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    bool is_current = ((uint32_t)pd_phys == current_cr3) || (pd_phys == NULL);

    uint32_t irq_flags = 0;
    if (!is_current) {
        irq_flags = spin_lock_irqsave(&temp_map_lock);
    }

    if (is_current) {
        pd_virt = (uint32_t *)0xFFFFF000;
    } else {
        pd_virt = (uint32_t *)map_temp_page(1, (paddr_t)pd_phys);
    }

    if (pd_virt[pd_idx] & PDE_PRESENT) {
        uint32_t pt_phys = pd_virt[pd_idx] & PAGE_MASK;
        if (is_current) {
            pt_virt = (uint32_t *)(0xFFC00000 + (pd_idx << 12));
        } else {
            pt_virt = (uint32_t *)map_temp_page(2, pt_phys);
        }

        if (pt_virt[pt_idx] & PTE_PRESENT) {
            paddr = (pt_virt[pt_idx] & PAGE_MASK) | (vaddr & 0xFFF);
        }

        if (!is_current) {
            unmap_temp_page(2);
        }
    }

    if (!is_current) {
        unmap_temp_page(1);
        spin_unlock_irqrestore(&temp_map_lock, irq_flags);
    }

    return paddr;
}

void vmm_switch_pd(void *pd_phys) {
    if (!pd_phys) {
        return;
    }
    load_cr3((uint32_t)pd_phys);
}

/* 声明进程终止函数 */
void process_terminate_current(int signal);

extern void console_emergency_mode(void);

/* 声明外部函数以获取当前进程信息 */
extern void       *process_get_current(void);
extern int         process_get_pid(void *proc);
extern const char *process_get_name(void *proc);
extern void       *process_get_page_dir(void *proc);

void vmm_page_fault(struct irq_regs *frame, vaddr_t vaddr) {
    /* 进入紧急模式,确保同步输出 */
    console_emergency_mode();

    uint32_t err_code  = frame->err_code;
    bool     from_user = (frame->cs & 0x03) == 3;

    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));

    /* 获取当前进程信息 */
    void       *proc         = process_get_current();
    const char *proc_name    = proc ? process_get_name(proc) : "?";
    int         proc_pid     = proc ? process_get_pid(proc) : -1;

    const char *reason;
    if (!(err_code & 0x01)) {
        reason = "Not Present";
    } else if (err_code & 0x08) {
        reason = "Reserved Bit Violation";
    } else if (err_code & 0x10) {
        reason = "Instruction Fetch";
    } else if (err_code & 0x04) {
        reason = "User Access Violation";
    } else if (err_code & 0x02) {
        reason = "Write Violation";
    } else {
        reason = "Protection Violation";
    }

    /* 读取 PDE 和 PTE(通过递归映射) */
    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);
    uint32_t pde    = ((uint32_t *)0xFFFFF000)[pd_idx];
    uint32_t pte    = 0;
    if (pde & PDE_PRESENT) {
        pte = ((uint32_t *)(0xFFC00000 + (pd_idx << 12)))[pt_idx];
    }

    kprintf("%R[PAGE FAULT]%N vaddr=0x%x EIP=0x%x err=0x%x (%s)\n", vaddr, frame->eip, err_code,
            reason);
    kprintf("  Process: %s (PID %d)\n", proc_name, proc_pid);
    kprintf("  CR3=0x%x PDE[%u]=0x%x PTE[%u]=0x%x\n", cr3, pd_idx, pde, pt_idx, pte);
    kprintf("  PDE flags: P=%d RW=%d U=%d | PTE flags: P=%d RW=%d U=%d\n", (pde & 1),
            (pde >> 1) & 1, (pde >> 2) & 1, (pte & 1), (pte >> 1) & 1, (pte >> 2) & 1);

    if (from_user) {
        process_terminate_current(14); /* SIGSEGV */
        /* 不返回 */
    }

    /* 内核态页错误 → panic */
    panic("Kernel Page Fault at 0x%x\n"
          "Error Code: 0x%x (%s)\n"
          "CR3: 0x%x PDE=0x%x PTE=0x%x",
          vaddr, err_code, reason, cr3, pde, pte);
}

void *vmm_get_kernel_pd(void) {
    return kernel_pd;
}
