#include <xnix/abi/syscall.h>

.section .text
.global _start
.extern main

_start:
    movl (%esp), %eax
    movl 4(%esp), %ebx

    pushl %ebx
    pushl %eax
    call main
    addl $8, %esp

    movl %eax, %ebx
    movl $SYS_EXIT, %eax
    int $0x80

1:  jmp 1b

.section .note.GNU-stack,"",@progbits
