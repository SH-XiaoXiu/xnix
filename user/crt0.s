.section .text
.global _start
.extern main
.extern sys_exit

_start:
    /* 调用 main */
    call main

    /* 使用 main 的返回值调用 exit */
    push %eax
    call sys_exit
    
    /* 不应该运行到这里 */
    hlt
