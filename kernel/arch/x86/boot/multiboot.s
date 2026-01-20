# Multiboot header 让QEMU/GRUB直接加载内核
# 使用GAS语法，可以直接用gcc编译

.set ALIGN,    1<<0             # 对齐加载的模块
.set MEMINFO,  1<<1             # 提供内存映射
.set FLAGS,    ALIGN | MEMINFO  # Multiboot标志
.set MAGIC,    0x1BADB002       # Multiboot魔数
.set CHECKSUM, -(MAGIC + FLAGS) # 校验和

# Multiboot头必须在文件开头的8KB内
.section .multiboot, "a"
.align 4
multiboot_header:
.long MAGIC
.long FLAGS
.long CHECKSUM

# 入口点紧跟multiboot头
.section .text
.global _start
.type _start, @function
_start:
    # 设置栈指针
    mov $stack_top, %esp

    # 清除EFLAGS
    pushl $0
    popf

    # 调用C内核入口
    call kernel_main

    # 如果kernel_main返回，死循环
    cli
1:  hlt
    jmp 1b
.size _start, . - _start

# 栈空间
.section .bss, "aw", @nobits
.align 16
stack_bottom:
.skip 16384                     # 16KB 栈
stack_top:

# 消除executable stack警告
.section .note.GNU-stack, "", @progbits
