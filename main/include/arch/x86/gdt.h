#ifndef ARCH_X86_GDT_H
#define ARCH_X86_GDT_H

/* 段选择子定义 */
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS   0x1B /* Index 3, RPL 3 */
#define USER_DS   0x23 /* Index 4, RPL 3 */
#define TSS_SEG   0x28 /* Index 5 */

#endif
