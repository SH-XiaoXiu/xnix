# crt0.s - C Runtime 入口
# 用户程序入口点，调用 main 后退出

.section .text
.global _start
.extern main

_start:
    # 栈上有: argc, argv
    # [esp+0] = argc
    # [esp+4] = argv

    # 读取 argc 和 argv
    movl (%esp), %eax       # argc
    movl 4(%esp), %ebx      # argv

    # 调用 main(argc, argv)
    pushl %ebx              # argv
    pushl %eax              # argc
    call main
    addl $8, %esp           # 清理参数

    # main 返回后调用 exit(返回值)
    movl %eax, %ebx         # exit code = main 返回值
    movl $2, %eax           # SYS_EXIT = 2
    int $0x80

    # 不应到达这里
1:  jmp 1b

# 标记栈为不可执行
.section .note.GNU-stack,"",@progbits
