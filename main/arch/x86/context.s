/**
 * @file context.s
 * @brief 上下文切换
 * @author XiaoXiu
 */

.section .text
.code32

/**
 * void context_switch(struct task_context *old, struct task_context *new)
 *
 * task_context 结构:
 *   0: esp
 *   4: ebp
 *   8: ebx
 *  12: esi
 *  16: edi
 *  20: eip (未使用，仅调试)
 */
.global context_switch
context_switch:
    /* 获取参数 */
    mov 4(%esp), %eax       /* old */
    mov 8(%esp), %edx       /* new */

    /* 保存 callee-saved 寄存器到 old */
    mov %esp, 0(%eax)
    mov %ebp, 4(%eax)
    mov %ebx, 8(%eax)
    mov %esi, 12(%eax)
    mov %edi, 16(%eax)

    /* 从 new 恢复 callee-saved 寄存器 */
    mov 0(%edx), %esp
    mov 4(%edx), %ebp
    mov 8(%edx), %ebx
    mov 12(%edx), %esi
    mov 16(%edx), %edi

    /* ret 会从新栈弹出返回地址 */
    ret

/**
 * void context_switch_first(struct task_context *new)
 * 第一次启动任务，不保存旧上下文
 * 注意不在此处开中断，由 thread_entry_wrapper 开启
 * 这样确保 sched_cleanup_zombie 在中断关闭的状态下执行
 */
.global context_switch_first
context_switch_first:
    mov 4(%esp), %edx       /* new */

    /* 从 new 恢复 callee-saved 寄存器 */
    mov 0(%edx), %esp
    mov 4(%edx), %ebp
    mov 8(%edx), %ebx
    mov 12(%edx), %esi
    mov 16(%edx), %edi

    /* 不在此处开中断，thread_entry_wrapper 会处理 */
    ret

/**
 * void enter_user_mode(uint32_t eip, uint32_t esp)
 * 切换到用户模式
 */
.global enter_user_mode
enter_user_mode:
    mov 4(%esp), %ebx  /* eip */
    mov 8(%esp), %ecx  /* esp */

    /* 设置数据段 (USER_DS | RPL3) = 0x20 | 3 = 0x23 */
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* 构造 IRET 栈帧: SS, ESP, EFLAGS, CS, EIP */
    push $0x23         /* SS */
    push %ecx          /* ESP */
    pushf
    pop %eax
    or $0x200, %eax    /* Enable Interrupts (IF) */
    push %eax          /* EFLAGS */
    push $0x1B         /* CS (USER_CS | RPL3) = 0x18 | 3 = 0x1B */
    push %ebx          /* EIP */

    /* 切换! */
    iret

/* 栈保护标记 */
.section .note.GNU-stack, "", @progbits
