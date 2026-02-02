#ifndef ASM_MULTIBOOT_H
#define ASM_MULTIBOOT_H

#include <xnix/types.h>

/* Multiboot 1 Magic */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Multiboot Info Flags */
#define MULTIBOOT_INFO_MEMORY      0x001
#define MULTIBOOT_INFO_BOOTDEV     0x002
#define MULTIBOOT_INFO_CMDLINE     0x004
#define MULTIBOOT_INFO_MODS        0x008
#define MULTIBOOT_INFO_AOUT_SYMS   0x010
#define MULTIBOOT_INFO_ELF_SHDR    0x020
#define MULTIBOOT_INFO_MEM_MAP     0x040
#define MULTIBOOT_INFO_DRIVES      0x080
#define MULTIBOOT_INFO_CONFIG      0x100
#define MULTIBOOT_INFO_BOOTLOADER  0x200
#define MULTIBOOT_INFO_APM         0x400
#define MULTIBOOT_INFO_VBE         0x800
#define MULTIBOOT_INFO_FRAMEBUFFER 0x1000

#define MULTIBOOT_MEMORY_AVAILABLE 1

/* Framebuffer 类型 */
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED  0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB      1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT 2

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
    uint32_t flags;       /* 偏移  0 */
    uint32_t mem_lower;   /* 偏移  4 */
    uint32_t mem_upper;   /* 偏移  8 */
    uint32_t boot_device; /* 偏移 12 */
    uint32_t cmdline;     /* 偏移 16 */
    uint32_t mods_count;  /* 偏移 20 */
    uint32_t mods_addr;   /* 偏移 24 */
    union {               /* 偏移 28 */
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
    uint32_t mmap_length;        /* 偏移 44 */
    uint32_t mmap_addr;          /* 偏移 48 */
    uint32_t drives_length;      /* 偏移 52 */
    uint32_t drives_addr;        /* 偏移 56 */
    uint32_t config_table;       /* 偏移 60 */
    uint32_t boot_loader_name;   /* 偏移 64 */
    uint32_t apm_table;          /* 偏移 68 */
    uint32_t vbe_control_info;   /* 偏移 72 */
    uint32_t vbe_mode_info;      /* 偏移 76 */
    uint16_t vbe_mode;           /* 偏移 80 */
    uint16_t vbe_interface_seg;  /* 偏移 82 */
    uint16_t vbe_interface_off;  /* 偏移 84 */
    uint16_t vbe_interface_len;  /* 偏移 86 */
    uint64_t framebuffer_addr;   /* 偏移 88 */
    uint32_t framebuffer_pitch;  /* 偏移 96 */
    uint32_t framebuffer_width;  /* 偏移 100 */
    uint32_t framebuffer_height; /* 偏移 104 */
    uint8_t  framebuffer_bpp;    /* 偏移 108 */
    uint8_t  framebuffer_type;   /* 偏移 109 */
    union {
        struct {
            uint32_t palette_addr;
            uint16_t palette_num_colors;
        } indexed;
        struct {
            uint8_t red_field_position;
            uint8_t red_mask_size;
            uint8_t green_field_position;
            uint8_t green_mask_size;
            uint8_t blue_field_position;
            uint8_t blue_mask_size;
        } rgb;
    } color_info;
} __attribute__((packed));

#endif
