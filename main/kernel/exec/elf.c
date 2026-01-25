/**
 * @file elf.c
 * @brief ELF32 加载器实现
 */

#include <kernel/process/process.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
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
#define USER_STACK_SIZE 8192
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

int process_load_elf(struct process *proc, void *elf_data, uint32_t elf_size) {
    if (!proc || !elf_data) {
        return -EINVAL;
    }

    /*
     * elf_data 是物理地址。
     * 由于 vmm_init() 已经建立了物理内存的恒等映射，
     * 我们可以直接访问该地址。
     */

    /* 确保数据足够读取头部 */
    if (elf_size < sizeof(Elf32_Ehdr)) {
        return -EINVAL;
    }

    Elf32_Ehdr *hdr = (Elf32_Ehdr *)elf_data;

    if (elf_verify_header(hdr) != 0) {
        pr_err("Invalid ELF header");
        return -EINVAL;
    }

    /* 遍历 Program Headers */
    Elf32_Phdr *phdr = (Elf32_Phdr *)((uint8_t *)elf_data + hdr->e_phoff);
    for (int i = 0; i < hdr->e_phnum; i++, phdr++) {
        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        /* 映射每一页 */
        uint32_t vaddr_start = phdr->p_vaddr;
        uint32_t vaddr_end   = vaddr_start + phdr->p_memsz;
        uint32_t page_start  = PAGE_ALIGN_DOWN(vaddr_start);
        uint32_t page_end    = PAGE_ALIGN_UP(vaddr_end);

        for (uint32_t vaddr = page_start; vaddr < page_end; vaddr += PAGE_SIZE) {
            /* 检查是否已经映射 */
            paddr_t exist_paddr = vmm_get_paddr(proc->page_dir_phys, vaddr);
            if (!exist_paddr) {
                void *page = alloc_page();
                if (!page) {
                    return -ENOMEM;
                }

                /* 映射到用户空间 */
                uint32_t flags = VMM_PROT_USER | VMM_PROT_READ;
                if (phdr->p_flags & PF_W) {
                    flags |= VMM_PROT_WRITE;
                }
                /* 总是给写权限以便初始化数据 */
                flags |= VMM_PROT_WRITE;

                vmm_map_page(proc->page_dir_phys, vaddr, (paddr_t)page, flags);

                /* 清空页面 */
                memset(page, 0, PAGE_SIZE);
            }
        }

        /* 拷贝文件内容 */
        uint32_t file_offset = phdr->p_offset;
        uint32_t file_len    = phdr->p_filesz;
        uint32_t vaddr       = phdr->p_vaddr;

        uint32_t copied = 0;
        while (copied < file_len) {
            uint32_t page_vaddr  = PAGE_ALIGN_DOWN(vaddr + copied);
            uint32_t page_offset = (vaddr + copied) & (PAGE_SIZE - 1);
            uint32_t chunk_size  = PAGE_SIZE - page_offset;
            if (copied + chunk_size > file_len) {
                chunk_size = file_len - copied;
            }

            paddr_t paddr = vmm_get_paddr(proc->page_dir_phys, page_vaddr);
            if (!paddr) {
                return -EFAULT;
            }

            /* 直接拷贝物理内存 */
            /* 使用 kmap 访问目标物理页，以防目标页在 HighMem */
            void *dest_page = vmm_kmap(paddr);
            void *dest      = (void *)((uint32_t)dest_page + page_offset);

            /*
             * 源数据访问：
             * elf_data 是物理地址。目前内核已恒等映射了所有探测到的物理内存，
             * 因此可以直接将其作为虚拟地址访问。
             */
            const void *src = (const void *)((uint32_t)elf_data + file_offset + copied);

            memcpy(dest, src, chunk_size);

            vmm_kunmap(dest_page);

            copied += chunk_size;
        }
    }

    /* 分配并映射用户栈 */
    void *stack_page_1 = alloc_page();
    void *stack_page_2 = alloc_page();
    if (!stack_page_1 || !stack_page_2) {
        return -ENOMEM;
    }

    /* 映射两页栈 (8KB) */
    vmm_map_page(proc->page_dir_phys, USER_STACK_TOP - 4096, (paddr_t)stack_page_1,
                 VMM_PROT_USER | VMM_PROT_READ | VMM_PROT_WRITE);
    vmm_map_page(proc->page_dir_phys, USER_STACK_TOP - 8192, (paddr_t)stack_page_2,
                 VMM_PROT_USER | VMM_PROT_READ | VMM_PROT_WRITE);

    /* 清空栈 */
    memset(stack_page_1, 0, PAGE_SIZE);
    memset(stack_page_2, 0, PAGE_SIZE);

    pr_info("ELF loaded, entry point %x", hdr->e_entry);
    return (int)hdr->e_entry;
}
