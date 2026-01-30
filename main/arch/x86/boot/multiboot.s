# Multiboot header 让QEMU/GRUB直接加载内核
# 使用GAS语法，可以直接用gcc编译

.set ALIGN,    1<<0             # 对齐加载的模块
.set MEMINFO,  1<<1             # 提供内存映射
.set FLAGS,    ALIGN | MEMINFO  # Multiboot标志
.set MAGIC,    0x1BADB002       # Multiboot魔数
.set CHECKSUM, -(MAGIC + FLAGS) # 校验和
.set KERNEL_VIRT_BASE, 0xC0000000  # 内核虚拟基址

# Multiboot头必须在文件开头的8KB内
.section .multiboot, "a"
.align 4
multiboot_header:
.long MAGIC
.long FLAGS
.long CHECKSUM

# 启动阶段临时页表（.boot.data 段，链接到低物理地址）
# 映射 64MB: 16个页表 + 1个页目录 + 1个低地址页表 = 72KB
.section .boot.data, "aw", @progbits
.align 4096

boot_page_directory:
.skip 4096

boot_page_table_low:     # 映射低 4MB (0x0 -> 0x0)
.skip 4096

# 高地址映射：16个页表，映射 64MB (0xC0000000 -> 0x0)
boot_page_tables_high:
.skip 65536              # 16 * 4096 = 65536 字节

# 入口点
.section .text
.global _start
.type _start, @function
_start:
    # 关闭中断
    cli

    # 保存 multiboot 信息到栈（临时）
    push %ebx

    # 初始化低地址页表（恒等映射 0-4MB）
    mov $boot_page_table_low, %edi
    sub $KERNEL_VIRT_BASE, %edi
    xor %esi, %esi
    mov $1024, %ecx
1:
    mov %esi, %eax
    or $0x003, %eax                   # Present + RW
    mov %eax, (%edi)
    add $4096, %esi
    add $4, %edi
    loop 1b

    # 初始化高地址页表（映射 64MB: 0xC0000000-0xC3FFFFFF -> 0x0-0x3FFFFFF）
    mov $boot_page_tables_high, %edi
    sub $KERNEL_VIRT_BASE, %edi
    xor %esi, %esi
    mov $16384, %ecx                  # 16个页表 * 1024项 = 16384项
2:
    mov %esi, %eax
    or $0x003, %eax
    mov %eax, (%edi)
    add $4096, %esi
    add $4, %edi
    loop 2b

    # 设置页目录（转换为物理地址）
    mov $boot_page_directory, %edi
    sub $KERNEL_VIRT_BASE, %edi

    # 清空页目录
    push %edi
    mov $1024, %ecx
    xor %eax, %eax
    rep stosl
    pop %edi

    # PDE[0] -> 低地址页表（恒等映射）
    mov $boot_page_table_low, %eax
    sub $KERNEL_VIRT_BASE, %eax
    or $0x003, %eax
    mov %eax, (%edi)

    # PDE[768-783] -> 16个高地址页表
    mov $boot_page_tables_high, %eax
    sub $KERNEL_VIRT_BASE, %eax
    lea 3072(%edi), %edx              # &PDE[768] = edi + 768*4
    mov $16, %ecx
3:
    mov %eax, %esi
    or $0x003, %esi
    mov %esi, (%edx)
    add $4096, %eax                   # 下一个页表
    add $4, %edx                      # 下一个 PDE
    loop 3b

    # 加载 CR3（物理地址）
    mov $boot_page_directory, %eax
    sub $KERNEL_VIRT_BASE, %eax
    mov %eax, %cr3

    # 启用分页 (CR0.PG = bit 31)
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    # 恢复 multiboot 信息
    pop %ebx

    # 跳转到高地址
    lea higher_half_entry, %ecx
    jmp *%ecx

higher_half_entry:
    # 现在运行在高地址

    # 设置栈指针
    mov $stack_top, %esp

    # 清除 EFLAGS
    pushl $0
    popf

    # 保存 Multiboot 信息指针（EBX 是物理地址，转换为虚拟地址）
    mov %ebx, %eax
    add $KERNEL_VIRT_BASE, %eax
    mov %eax, multiboot_info_ptr

    # 传递参数给 kernel_main
    pushl %eax                        # multiboot_info (虚拟地址)
    pushl $0x2BADB002                 # magic

    # 调用内核入口
    call kernel_main

    # 不应返回
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

# Multiboot 信息指针
.global multiboot_info_ptr
multiboot_info_ptr:
.skip 4

# 消除executable stack警告
.section .note.GNU-stack, "", @progbits
