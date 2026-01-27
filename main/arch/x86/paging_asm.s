.global load_cr3
load_cr3:
    mov 4(%esp), %eax
    mov %eax, %cr3
    ret

.global enable_paging
enable_paging:
    mov %cr3, %eax      # 这是一个 dummy read, 确保 cr3 已设置
    mov %cr0, %eax
    or  $0x80000000, %eax # Set PG bit (31)
    mov %eax, %cr0
    ret

# 标记栈为不可执行
.section .note.GNU-stack,"",@progbits
