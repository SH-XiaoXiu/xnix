/* 内核链接脚本 */
ENTRY(_start)

SECTIONS
{
    . = 1M;  /* 内核加载地址: 1MB */

    /* Multiboot头必须在文件最开始的8KB内 */
    .multiboot : ALIGN(4) {
        *(.multiboot)
    }

    /* 代码段 */
    .text : ALIGN(4K) {
        *(.text)
    }

    /* 只读数据 */
    .rodata : ALIGN(4K) {
        *(.rodata*)
    }

    /* 初始化数据 */
    .data : ALIGN(4K) {
        *(.data)
    }

    /* 未初始化数据 */
    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss)
    }
}