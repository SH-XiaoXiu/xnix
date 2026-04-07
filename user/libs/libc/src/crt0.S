#include <xnix/abi/syscall.h>

.section .text
.global _start
.extern main
.extern __libc_init
.extern __libc_fini

_start:
    movl (%esp), %eax
    movl 4(%esp), %ebx

    pushl %ebx
    pushl %eax

    call __libc_init

    popl %eax
    popl %ebx
    pushl %ebx
    pushl %eax
    call main
    addl $8, %esp

    movl %eax, %ebx
    pushl %ebx
    call __libc_fini
    popl %ebx
    movl $SYS_EXIT, %eax
    int $0x80

1:  jmp 1b

.section .note.GNU-stack,"",@progbits
