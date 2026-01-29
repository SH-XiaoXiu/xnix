#ifndef ASM_MULTIBOOT_H
#define ASM_MULTIBOOT_H

#include <xnix/types.h>

/* Multiboot 1 Magic */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Multiboot Info Flags */
#define MULTIBOOT_INFO_MEMORY    0x001
#define MULTIBOOT_INFO_BOOTDEV   0x002
#define MULTIBOOT_INFO_CMDLINE   0x004
#define MULTIBOOT_INFO_MODS      0x008
#define MULTIBOOT_INFO_AOUT_SYMS 0x010
#define MULTIBOOT_INFO_ELF_SHDR  0x020
#define MULTIBOOT_INFO_MEM_MAP   0x040

#define MULTIBOOT_MEMORY_AVAILABLE 1

struct multiboot_mod_list {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
};

struct multiboot_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    union {
        struct {
            uint32_t tabsize;
            uint32_t strsize;
            uint32_t addr;
            uint32_t reserved;
        } aout_sym;
        struct {
            uint32_t num;
            uint32_t size;
            uint32_t addr;
            uint32_t shndx;
        } elf_sec;
    } u;
    uint32_t mmap_length;
    uint32_t mmap_addr;
};

#endif
