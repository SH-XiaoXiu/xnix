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

    sti                     /* 开中断 */
    ret
