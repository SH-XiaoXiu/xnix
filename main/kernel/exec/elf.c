/**
 * @file elf.c
 * @brief ELF32 加载器实现
 */

#include <kernel/process/process.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/vmm.h>

/* ELF Header Constants */
#define EI_NIDENT 16
#define ELFMAG0   0x7F
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'

#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define EV_CURRENT  1
#define ET_EXEC     2
#define EM_386      3

/* Program Header Types */
#define PT_LOAD 1

/* Program Header Flags */
#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef uint32_t Elf32_Addr;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef int32_t  Elf32_Sword;
typedef uint32_t Elf32_Word;

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

/* 声明架构相关的用户态跳转函数 */
extern void enter_user_mode(uint32_t eip, uint32_t esp);

/* 栈大小 */
#define USER_STACK_SIZE (64 * 1024)
#define USER_STACK_TOP  0xBFFFF000

/**
 * 验证 ELF 头
 */
static int elf_verify_header(Elf32_Ehdr *hdr) {
    if (hdr->e_ident[0] != ELFMAG0 || hdr->e_ident[1] != ELFMAG1 || hdr->e_ident[2] != ELFMAG2 ||
        hdr->e_ident[3] != ELFMAG3) {
        return -EINVAL;
    }
    if (hdr->e_ident[4] != ELFCLASS32) {
        return -EINVAL;
    }
    if (hdr->e_ident[5] != ELFDATA2LSB) {
        return -EINVAL;
    }
    if (hdr->e_type != ET_EXEC) {
        return -EINVAL;
    }
    if (hdr->e_machine != EM_386) {
        return -EINVAL;
    }
    if (hdr->e_version != EV_CURRENT) {
        return -EINVAL;
    }
    return 0;
}

/* 声明 vmm_kmap/kunmap */
extern void *vmm_kmap(paddr_t paddr);
extern void  vmm_kunmap(void *vaddr);

static int elf_memcpy_from_phys(void *dst, uint64_t src_phys, uint32_t len) {
    if (!dst || !len) {
        return 0;
    }

    uint8_t *out    = (uint8_t *)dst;
    uint32_t copied = 0;
    while (copied < len) {
        uint64_t cur = src_phys + copied;
        if (cur > 0xFFFFFFFFULL) {
            return -EFAULT;
        }

        paddr_t  page_paddr  = (paddr_t)(cur & ~(uint64_t)(PAGE_SIZE - 1));
        uint32_t page_offset = (uint32_t)(cur & (PAGE_SIZE - 1));
        uint32_t chunk       = PAGE_SIZE - page_offset;
        uint32_t remain      = len - copied;
        if (chunk > remain) {
            chunk = remain;
        }

        void *mapped = vmm_kmap(page_paddr);
        memcpy(out + copied, (uint8_t *)mapped + page_offset, chunk);
        vmm_kunmap(mapped);

        copied += chunk;
    }

    return 0;
}

static int elf_memcpy_phys_to_phys(uint64_t dst_phys, uint64_t src_phys, uint32_t len) {
    if (!len) {
        return 0;
    }

    uint8_t  buf[256];
    uint32_t copied = 0;
    while (copied < len) {
        uint64_t cur_src = src_phys + copied;
        uint64_t cur_dst = dst_phys + copied;
        if (cur_src > 0xFFFFFFFFULL || cur_dst > 0xFFFFFFFFULL) {
            return -EFAULT;
        }

        paddr_t  src_page = (paddr_t)(cur_src & ~(uint64_t)(PAGE_SIZE - 1));
        uint32_t src_off  = (uint32_t)(cur_src & (PAGE_SIZE - 1));
        paddr_t  dst_page = (paddr_t)(cur_dst & ~(uint64_t)(PAGE_SIZE - 1));
        uint32_t dst_off  = (uint32_t)(cur_dst & (PAGE_SIZE - 1));

        uint32_t chunk    = len - copied;
        uint32_t src_room = PAGE_SIZE - src_off;
        uint32_t dst_room = PAGE_SIZE - dst_off;
        if (chunk > src_room) {
            chunk = src_room;
        }
        if (chunk > dst_room) {
            chunk = dst_room;
        }
        if (chunk > sizeof(buf)) {
            chunk = (uint32_t)sizeof(buf);
        }

        void *src_mapped = vmm_kmap(src_page);
        memcpy(buf, (uint8_t *)src_mapped + src_off, chunk);
        vmm_kunmap(src_mapped);

        void *dst_mapped = vmm_kmap(dst_page);
        memcpy((uint8_t *)dst_mapped + dst_off, buf, chunk);
        vmm_kunmap(dst_mapped);

        copied += chunk;
    }

    return 0;
}

int process_load_elf(struct process *proc, void *elf_data, uint32_t elf_size, uint32_t *out_entry) {
    if (!proc || !elf_data || !out_entry) {
        return -EINVAL;
    }

    if (!elf_size) {
        return -EINVAL;
    }

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->map || !mm->query) {
        return -EFAULT;
    }

    /* 确保数据足够读取头部 */
    if (elf_size < sizeof(Elf32_Ehdr)) {
        return -EINVAL;
    }

    paddr_t elf_paddr = (paddr_t)(uintptr_t)elf_data;

    Elf32_Ehdr hdr;
    int        ret = elf_memcpy_from_phys(&hdr, (uint64_t)elf_paddr, sizeof(hdr));
    if (ret < 0) {
        return ret;
    }

    if (elf_verify_header(&hdr) != 0) {
        pr_err("Invalid ELF header");
        return -EINVAL;
    }

    if (hdr.e_phentsize != sizeof(Elf32_Phdr)) {
        return -EINVAL;
    }
    uint64_t ph_table_bytes = (uint64_t)hdr.e_phnum * (uint64_t)hdr.e_phentsize;
    uint64_t ph_table_end   = (uint64_t)hdr.e_phoff + ph_table_bytes;
    if (ph_table_end > elf_size) {
        return -EINVAL;
    }

    /* 记录最高的段结束地址，用于初始化堆 */
    uint32_t max_seg_end = 0;

    /* 遍历 Program Headers */
    for (uint16_t i = 0; i < hdr.e_phnum; i++) {
        Elf32_Phdr phdr;
        uint32_t   phdr_off = (uint32_t)((uint64_t)hdr.e_phoff + (uint64_t)i * sizeof(Elf32_Phdr));
        ret = elf_memcpy_from_phys(&phdr, (uint64_t)elf_paddr + phdr_off, sizeof(phdr));
        if (ret < 0) {
            return ret;
        }

        if (phdr.p_type != PT_LOAD) {
            continue;
        }

        if (phdr.p_filesz > phdr.p_memsz) {
            return -EINVAL;
        }
        uint64_t seg_end = (uint64_t)phdr.p_offset + (uint64_t)phdr.p_filesz;
        if (seg_end > elf_size) {
            return -EINVAL;
        }

        /* 映射每一页 */
        uint32_t vaddr_start = phdr.p_vaddr;
        uint32_t vaddr_end   = vaddr_start + phdr.p_memsz;
        if (vaddr_end < vaddr_start) {
            return -EINVAL;
        }
        uint32_t page_start = PAGE_ALIGN_DOWN(vaddr_start);
        uint32_t page_end   = PAGE_ALIGN_UP(vaddr_end);

        for (uint32_t vaddr = page_start; vaddr < page_end; vaddr += PAGE_SIZE) {
            /* 检查是否已经映射 */
            paddr_t exist_paddr = (paddr_t)mm->query(proc->page_dir_phys, vaddr);
            if (!exist_paddr) {
                void *page = alloc_page_high();
                if (!page) {
                    return -ENOMEM;
                }

                /* 映射到用户空间 */
                uint32_t flags = VMM_PROT_USER | VMM_PROT_READ;
                if (phdr.p_flags & PF_W) {
                    flags |= VMM_PROT_WRITE;
                }
                /* 总是给写权限以便初始化数据 */
                flags |= VMM_PROT_WRITE;

                if (mm->map(proc->page_dir_phys, vaddr, (paddr_t)page, flags) != 0) {
                    free_page(page);
                    return -ENOMEM;
                }

                /* 清空页面 */
                void *k = vmm_kmap((paddr_t)page);
                memset(k, 0, PAGE_SIZE);
                vmm_kunmap(k);
            }
        }

        /* 拷贝文件内容 */
        uint32_t file_offset = phdr.p_offset;
        uint32_t file_len    = phdr.p_filesz;
        uint32_t vaddr       = phdr.p_vaddr;

        uint32_t copied = 0;
        while (copied < file_len) {
            uint32_t page_vaddr  = PAGE_ALIGN_DOWN(vaddr + copied);
            uint32_t page_offset = (vaddr + copied) & (PAGE_SIZE - 1);
            uint32_t chunk_size  = PAGE_SIZE - page_offset;
            if (copied + chunk_size > file_len) {
                chunk_size = file_len - copied;
            }

            paddr_t paddr = (paddr_t)mm->query(proc->page_dir_phys, page_vaddr);
            if (!paddr) {
                return -EFAULT;
            }

            uint64_t dst_phys = (uint64_t)(paddr & PAGE_MASK) + page_offset;
            uint64_t src_phys = (uint64_t)elf_paddr + (uint64_t)file_offset + copied;
            ret               = elf_memcpy_phys_to_phys(dst_phys, src_phys, chunk_size);
            if (ret < 0) {
                return ret;
            }

            copied += chunk_size;
        }

        /* 更新最高段结束地址 */
        if (vaddr_end > max_seg_end) {
            max_seg_end = vaddr_end;
        }
    }

    /* 初始化用户堆 */
    uint32_t heap_start = PAGE_ALIGN_UP(max_seg_end);
    uint32_t heap_max   = USER_STACK_TOP - USER_STACK_SIZE; /* 栈底之前 */
    proc->heap_start    = heap_start;
    proc->heap_current  = heap_start;
    proc->heap_max      = heap_max;

    /* 分配并映射用户栈 */
    uint32_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    void    *stack_mem[USER_STACK_SIZE / PAGE_SIZE];
    memset(stack_mem, 0, sizeof(stack_mem));
    for (uint32_t i = 1; i <= stack_pages; i++) {
        void *page = alloc_page_high();
        if (!page) {
            goto stack_fail;
        }

        vaddr_t vaddr = USER_STACK_TOP - (i * PAGE_SIZE);
        if (mm->map(proc->page_dir_phys, vaddr, (paddr_t)page,
                    VMM_PROT_USER | VMM_PROT_READ | VMM_PROT_WRITE) != 0) {
            free_page(page);
            goto stack_fail;
        }

        void *k = vmm_kmap((paddr_t)page);
        memset(k, 0, PAGE_SIZE);
        vmm_kunmap(k);

        stack_mem[i - 1] = page;
    }

    pr_info("ELF loaded, entry point %x", hdr.e_entry);
    *out_entry = hdr.e_entry;
    return 0;

stack_fail:
    if (mm->unmap) {
        for (uint32_t i = 1; i <= stack_pages; i++) {
            if (stack_mem[i - 1]) {
                mm->unmap(proc->page_dir_phys, USER_STACK_TOP - (i * PAGE_SIZE));
            }
        }
    }
    for (uint32_t i = 0; i < stack_pages; i++) {
        if (stack_mem[i]) {
            free_page(stack_mem[i]);
        }
    }
    return -ENOMEM;
}
