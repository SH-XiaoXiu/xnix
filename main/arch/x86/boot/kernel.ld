/* 内核链接脚本 - 高半核设计 */
ENTRY(_start)

/* 内核虚拟地址基址 */
KERNEL_VIRT_BASE = 0xC0000000;

SECTIONS
{
    /* 内核加载到物理 1MB，链接到虚拟 0xC0100000 */
    . = KERNEL_VIRT_BASE + 1M;

    /* Multiboot 头物理地址必须在 1MB 处 */
    .multiboot : AT(1M) ALIGN(4) {
        *(.multiboot)
    }

    /* 丢弃可能干扰加载的段 */
    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.comment)
    }

    /* 虚拟地址对齐到 4KB */
    . = ALIGN(4K);

    /* 启动阶段数据（临时页表等），物理地址手动指定为 1MB + 4KB */
    .boot.data : AT(0x101000) {
        *(.boot.data)
    }

    /* 虚拟地址对齐到 4KB */
    . = ALIGN(4K);

    /* 代码段从虚拟 0xC0104000 开始，物理 0x104000 */
    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE) {
        *(.text)
    }

    /* 只读数据 */
    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE) ALIGN(4K) {
        *(.rodata*)
    }

    /* 初始化数据 */
    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE) ALIGN(4K) {
        *(.data)
    }

    /* 未初始化数据 */
    .bss : AT(ADDR(.bss) - KERNEL_VIRT_BASE) ALIGN(4K) {
        *(COMMON)
        *(.bss)
    }

    /* 内核结束地址（虚拟地址） */
    . = ALIGN(4K);
    _kernel_end = .;

    /* 提供物理地址符号供早期代码使用 */
    _kernel_phys_end = . - KERNEL_VIRT_BASE;
}
