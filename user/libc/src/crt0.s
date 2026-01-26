# crt0.s - C Runtime 入口
# 用户程序入口点，调用 main 后退出

.section .text
.global _start
.extern main

_start:
    # 栈已由内核设置好
    # 调用 main
    call main

    # main 返回后调用 exit(返回值)
    movl %eax, %ebx     # exit code = main 返回值
    movl $2, %eax       # SYS_EXIT = 2
    int $0x80

    # 不应到达这里
1:  jmp 1b
