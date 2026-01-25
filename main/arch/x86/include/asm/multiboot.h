/**
 * @file multiboot.h
 * @brief Multiboot 规范结构体定义
 *
 * GRUB 启动时会填充这个结构,包含内存映射等信息
 * 参考: https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
 */

#ifndef ASM_MULTIBOOT_H
#define ASM_MULTIBOOT_H

#include <xnix/types.h>

/* Multiboot info 结构体中的标志位 */
#define MB_INFO_MEMORY  0x001 /* mem_lower, mem_upper 有效 */
#define MB_INFO_MEM_MAP 0x040 /* mmap_* 有效 */

/* Multiboot info 结构体(由 GRUB 填充) */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower; /* 低端内存大小 (KB),从 0 到 640KB */
    uint32_t mem_upper; /* 高端内存大小 (KB),从 1MB 开始 */
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length; /* 内存映射表长度 */
    uint32_t mmap_addr;   /* 内存映射表地址 */
};

/* 内存映射表条目 */
struct multiboot_mmap_entry {
    uint32_t size;      /* 本条目大小(不含 size 字段本身) */
    uint64_t base_addr; /* 内存区域起始地址 */
    uint64_t length;    /* 内存区域长度 */
    uint32_t type;      /* 类型:1=可用,其他=保留 */
} __attribute__((packed));

#define MB_MMAP_TYPE_AVAILABLE 1

/* 启动时保存的 Multiboot info 指针 */
extern uint32_t multiboot_info_ptr;

#endif
