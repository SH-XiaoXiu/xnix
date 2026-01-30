#include <arch/cpu.h>
#include <arch/mmu.h>
#include <arch/smp.h>

#include <asm/irq_defs.h>
#include <asm/mmu.h>
#include <xnix/config.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
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

/*
 * Per-CPU 临时映射窗口管理
 *
 * 每个 CPU 使用独立的 PD 项和虚拟地址空间,消除多核锁竞争:
 * - CPU 0: PD[1020], 虚拟地址 0xFF780000-0xFF7FFFFF (4MB, 1024 页)
 * - CPU 1: PD[1019], 虚拟地址 0xFF700000-0xFF77FFFF (4MB, 1024 页)
 * - CPU 2: PD[1018], 虚拟地址 0xFF680000-0xFF6FFFFF (4MB, 1024 页)
 * - 不拉不拉不拉
 *
 * 每个 CPU 的窗口内有两个映射槽:
 * - WINDOW_1: PT[1023] - 用于映射 PD
 * - WINDOW_2: PT[1022] - 用于映射 PT (vmm_kmap 使用)
 *
 * 虚拟地址计算:
 * - PD 索引: BASE_PD_IDX - cpu_id
 * - 虚拟地址基址: (BASE_PD_IDX - cpu_id) * 4MB
 * - PT 访问地址(递归映射): 0xFFC00000 + (PD_IDX * 4096)
 */
/*
 * 临时映射窗口 PDE 索引
 * PDE[1019] 被 IOAPIC (0xFEC00000) 和 LAPIC (0xFEE00000) 使用
 * 所以我们从 PDE[1018] 开始,避免冲突
 */
#define BASE_PD_IDX          1018 /* CPU 0 使用 PD[1018],避开 PDE[1019] (APIC) */
#define TEMP_VADDR_BASE(cpu) (((BASE_PD_IDX - (cpu)) << 22))
#define TEMP_PT_VADDR(cpu)   (0xFFC00000 + ((BASE_PD_IDX - (cpu)) << 12))

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
 * 映射任意物理页到当前 CPU 的临时窗口
 * window_id: 1 或 2
 * 返回虚拟地址
 */
static void *map_temp_page(int window_id, paddr_t paddr) {
    cpu_id_t cpu = cpu_current_id();
    if (cpu >= CFG_MAX_CPUS) {
        cpu = 0;
    }

    /* 当前 CPU 的临时 PT 虚拟地址 */
    uint32_t *temp_pt_virt = (uint32_t *)TEMP_PT_VADDR(cpu);

    /* 选择 PT 表项和虚拟地址 */
    uint32_t pt_idx = (window_id == 1) ? 1023 : 1022;
    vaddr_t  vaddr  = TEMP_VADDR_BASE(cpu) + (pt_idx << 12);

    /* 映射物理页 */
    temp_pt_virt[pt_idx] = (paddr & PAGE_MASK) | PTE_PRESENT | PTE_RW;
    invlpg(vaddr);

    return (void *)vaddr;
}

static void unmap_temp_page(int window_id) {
    cpu_id_t cpu = cpu_current_id();
    if (cpu >= CFG_MAX_CPUS) {
        cpu = 0;
    }

    /* 当前 CPU 的临时 PT 虚拟地址 */
    uint32_t *temp_pt_virt = (uint32_t *)TEMP_PT_VADDR(cpu);

    /* 选择 PT 表项和虚拟地址 */
    uint32_t pt_idx = (window_id == 1) ? 1023 : 1022;
    vaddr_t  vaddr  = TEMP_VADDR_BASE(cpu) + (pt_idx << 12);

    /* 清除映射 */
    temp_pt_virt[pt_idx] = 0;
    invlpg(vaddr);
}

/* Per-CPU 中断标志保存 */
static DEFINE_PER_CPU(uint32_t, kmap_irq_flags);

/*
 * 临时映射 (HighMem access)
 *
 * 使用当前 CPU 的 WINDOW_2 映射任意物理页,以便内核访问.
 * 每个 CPU 使用独立的窗口,无需全局锁,只需禁用中断防止重入.
 */
void *vmm_kmap(paddr_t paddr) {
    uint32_t flags = cpu_irq_save();
    this_cpu_write(kmap_irq_flags, flags);
    return map_temp_page(2, paddr);
}

void vmm_kunmap(void *vaddr) {
    (void)vaddr;
    unmap_temp_page(2);
    uint32_t flags = this_cpu_read(kmap_irq_flags);
    cpu_irq_restore(flags);
}

void vmm_init(void) {
    /* 分配内核页目录(物理地址) */
    paddr_t kernel_pd_phys = (paddr_t)alloc_page();
    if (!kernel_pd_phys) {
        panic("Failed to allocate kernel page directory");
    }

    /* 转换为虚拟地址以便访问 */
    uint32_t *kernel_pd_virt = PHYS_TO_VIRT(kernel_pd_phys);
    memset(kernel_pd_virt, 0, PAGE_SIZE);

    /* 内核直接映射:0xC0000000 -> 物理 0x0 */
    paddr_t start, end;
    arch_get_memory_range(&start, &end);

    uint32_t map_size = KERNEL_DIRECT_MAP_SIZE;
    if (end < map_size) {
        map_size = (end + 0x3FFFFF) & ~0x3FFFFF;
    }

    uint32_t pt_count = map_size >> 22; /* 每个 PT 覆盖 4MB */

    for (uint32_t i = 0; i < pt_count; i++) {
        paddr_t pt_phys = (paddr_t)alloc_page();
        if (!pt_phys) {
            pr_warn("Partial kernel PT allocated");
            break;
        }

        uint32_t *pt_virt = PHYS_TO_VIRT(pt_phys);
        memset(pt_virt, 0, PAGE_SIZE);

        /* 填充页表:虚拟 0xC0000000 + i*4MB -> 物理 i*4MB */
        for (uint32_t j = 0; j < 1024; j++) {
            paddr_t paddr = (i * 1024 * 4096) + (j * 4096);
            if (paddr < end) {
                pt_virt[j] = paddr | PTE_PRESENT | PTE_RW;
            }
        }

        /* 写入 PDE[768 + i] */
        kernel_pd_virt[768 + i] = pt_phys | PDE_PRESENT | PDE_RW;
    }

    /* Per-CPU 临时映射窗口 */
    for (uint32_t cpu = 0; cpu < CFG_MAX_CPUS; cpu++) {
        paddr_t temp_pt_phys = (paddr_t)alloc_page();
        if (!temp_pt_phys) {
            panic("Failed to allocate temp PT for CPU %u", cpu);
        }
        uint32_t *temp_pt_virt = PHYS_TO_VIRT(temp_pt_phys);
        memset(temp_pt_virt, 0, PAGE_SIZE);

        uint32_t pd_idx        = BASE_PD_IDX - cpu;
        kernel_pd_virt[pd_idx] = temp_pt_phys | PDE_PRESENT | PDE_RW;
    }

    /* 递归映射:PDE[1023] -> PD 自身 */
    kernel_pd_virt[1023] = kernel_pd_phys | PDE_PRESENT | PDE_RW;

    /*
     * 保留低端恒等映射(PDE[0])用于:
     * - SMP trampoline 代码(0x8000)
     * - VGA 缓冲区(0xB8000)
     *
     * 方案:从 boot 页表复制 PDE[0],如果失败则手动创建
     */
    uint32_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    uint32_t *boot_pd_virt = PHYS_TO_VIRT(current_cr3);

    bool pde0_copied = false;
    if (boot_pd_virt[0] & PDE_PRESENT) {
        kernel_pd_virt[0] = boot_pd_virt[0];
        pde0_copied       = true;
    }

    /* 如果复制失败,手动创建低 4MB 恒等映射 */
    if (!pde0_copied) {
        paddr_t pt0_phys = (paddr_t)alloc_page();
        if (pt0_phys) {
            uint32_t *pt0_virt = PHYS_TO_VIRT(pt0_phys);
            memset(pt0_virt, 0, PAGE_SIZE);
            /* 映射低 4MB (0x0 - 0x400000) */
            for (uint32_t j = 0; j < 1024; j++) {
                paddr_t paddr = j * PAGE_SIZE;
                pt0_virt[j]   = paddr | PTE_PRESENT | PTE_RW;
            }
            kernel_pd_virt[0] = pt0_phys | PDE_PRESENT | PDE_RW;
        } else {
        }
    }

    /* 保存内核 PD 物理地址 */
    kernel_pd = (uint32_t *)kernel_pd_phys;

    /* 切换到新页目录 */
    load_cr3(kernel_pd_phys);

    pr_ok("VMM initialized, Kernel at 0x%x, mapped %u MB", KERNEL_VIRT_BASE,
          map_size / 1024 / 1024);
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

    /* 如果不是当前 PD,需要禁用中断使用临时窗口 */
    uint32_t irq_flags = 0;
    if (!is_current) {
        irq_flags = cpu_irq_save();
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
                cpu_irq_restore(irq_flags);
            }
            return -ENOMEM;
        }

        /* 清零新页表 - 使用窗口 2 */

        uint32_t temp_irq_flags = 0;
        if (is_current) {
            temp_irq_flags = cpu_irq_save();
        }

        void *ptr = map_temp_page(2, (paddr_t)new_pt_phys);
        memset(ptr, 0, PAGE_SIZE);
        unmap_temp_page(2);

        if (is_current) {
            cpu_irq_restore(temp_irq_flags);
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
        cpu_irq_restore(irq_flags);
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
        irq_flags = cpu_irq_save();
    }

    if (is_current) {
        pd_virt = (uint32_t *)0xFFFFF000;
    } else {
        pd_virt = (uint32_t *)map_temp_page(1, (paddr_t)pd_phys);
    }

    if (!(pd_virt[pd_idx] & PDE_PRESENT)) {
        if (!is_current) {
            unmap_temp_page(1);
            cpu_irq_restore(irq_flags);
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
        cpu_irq_restore(irq_flags);
    }
}

void *vmm_create_pd(void) {
    uint32_t *pd_phys = (uint32_t *)alloc_page();
    if (!pd_phys) {
        return NULL;
    }

    /* 映射新 PD 到窗口 1 进行初始化 */
    uint32_t  irq_flags = cpu_irq_save();
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
     * 内核运行在高半核(0xC0000000+),用户进程陷入内核时(中断/系统调用)
     * 需要访问内核代码,所以必须拷贝内核映射.
     */

    /* 拷贝 PDE[0]:低 4MB 恒等映射,用于 VGA 输出和 AP trampoline */
    if (kpd_virt[0] & PDE_PRESENT) {
        pd_virt[0] = kpd_virt[0];
    }

    /* 拷贝内核映射:PDE[768-1022] */
    for (int i = 768; i < 1023; i++) {
        if (kpd_virt[i] & PDE_PRESENT) {
            pd_virt[i] = kpd_virt[i];
        }
    }

    /* 拷贝 Per-CPU 临时窗口 */
    for (uint32_t cpu = 0; cpu < CFG_MAX_CPUS; cpu++) {
        uint32_t pd_idx = BASE_PD_IDX - cpu;
        if (kpd_virt[pd_idx] & PDE_PRESENT) {
            pd_virt[pd_idx] = kpd_virt[pd_idx];
        }
    }

    /* 如果映射了 kernel_pd,需要解除映射 */
    if (!kernel_is_current) {
        unmap_temp_page(2);
    }

    /* 递归映射: 最后一项指向自己 */
    pd_virt[1023] = ((uint32_t)pd_phys) | PDE_PRESENT | PDE_RW;

    unmap_temp_page(1);
    cpu_irq_restore(irq_flags);
    return pd_phys;
}

void vmm_destroy_pd(void *pd_phys) {
    if (pd_phys == kernel_pd) {
        return; /* 不能销毁内核 PD */
    }

    uint32_t irq_flags = cpu_irq_save();

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
    cpu_irq_restore(irq_flags);

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
        irq_flags = cpu_irq_save();
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
        cpu_irq_restore(irq_flags);
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
    void       *proc      = process_get_current();
    const char *proc_name = proc ? process_get_name(proc) : "?";
    int         proc_pid  = proc ? process_get_pid(proc) : -1;

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
