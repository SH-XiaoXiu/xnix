#include <kernel/process/process.h>
#include <xnix/debug.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/vmm.h>

/* 硬编码的一个简单用户程序:
 * 循环打印 "User"
 */
static uint8_t init_code[] = {
    /* 00 */ 0xE8, 0x00, 0x00, 0x00, 0x00, /* call +0 (get EIP) */
    /* 05 */ 0x5E,                         /* pop esi */
    /* 06 */ 0x83, 0xC6, 0x20,             /* add esi, 32 (offset to string: 0x25 - 0x05 = 0x20) */

    /* loop_start: */
    /* 09 */ 0x56, /* push esi */

    /* print_loop: */
    /* 0A */ 0x0F, 0xB6, 0x1E, /* movzx ebx, byte [esi] */
    /* 0D */ 0x84, 0xDB,       /* test bl, bl */
    /* 0F */ 0x74, 0x0A,       /* jz delay (+10 -> 1B) */

    /* 11 */ 0xB8, 0x01, 0x00, 0x00, 0x00, /* mov eax, 1 */
    /* 16 */ 0xCD, 0x80,                   /* int 0x80 */
    /* 18 */ 0x46,                         /* inc esi */
    /* 19 */ 0xEB, 0xEF,                   /* jmp print_loop (-17) */

    /* delay: */
    /* 1B */ 0x5E,                         /* pop esi */
    /* 1C */ 0xB9, 0x00, 0x00, 0x00, 0x10, /* mov ecx, 0x10000000 (approx 268M loops) */

    /* delay_loop: */
    /* 21 */ 0xE2, 0xFE, /* loop -2 */
    /* 23 */ 0xEB, 0xE4, /* jmp loop_start (-28) */

    /* string_data (offset 25 = 0x19) */
    'U', 's', 'e', 'r', '\n', 0x00};

#define USER_STACK_SIZE 8192
#define USER_CODE_BASE  0x08048000
#define USER_STACK_TOP  0xBFFFF000

/*
 * 这是一个临时的加载器,后续会被 ELF 加载器替代
 * 它将硬编码的指令拷贝到进程地址空间并设置栈
 */
int process_load_user(struct process *proc, const char *path) {
    (void)path; /* 暂时忽略路径,直接加载硬编码代码 */

    if (!proc || !proc->page_dir_phys) {
        return -EINVAL;
    }


    /* 映射代码段 */
    /* 分配一页物理内存作为代码页 */
    void *code_page = alloc_page();
    if (!code_page) {
        return -ENOMEM;
    }

    /* 拷贝代码 */
    memcpy((void *)code_page, init_code, sizeof(init_code));
    /* 将物理页映射到用户空间 USER_CODE_BASE */
    /* flags: USER | READ | EXEC (x86 没有 NX 位通常是 RWX) */
    vmm_map_page(proc->page_dir_phys, USER_CODE_BASE, (paddr_t)code_page,
                 VMM_PROT_USER | VMM_PROT_READ | VMM_PROT_WRITE); // TODO: 稍后可能删除WRITE


    /* 映射栈段 */
    /* 分配栈物理页 */
    void *stack_page_1 = alloc_page();
    void *stack_page_2 = alloc_page();

    if (!stack_page_1 || !stack_page_2) {
        /* TODO: cleanup */
        return -ENOMEM;
    }

    /* 映射两页栈 */
    vmm_map_page(proc->page_dir_phys, USER_STACK_TOP - 4096, (paddr_t)stack_page_1,
                 VMM_PROT_USER | VMM_PROT_READ | VMM_PROT_WRITE);
    vmm_map_page(proc->page_dir_phys, USER_STACK_TOP - 8192, (paddr_t)stack_page_2,
                 VMM_PROT_USER | VMM_PROT_READ | VMM_PROT_WRITE);

    return 0;
}

/* 声明架构相关的用户态跳转函数 */
extern void enter_user_mode(uint32_t eip, uint32_t esp);

/*
 * 用户线程入口
 * 当线程第一次被调度时,会从这里开始执行 (通过 context_switch_first -> thread_entry_wrapper ->
 * entry)
 */
void user_thread_entry(void *arg) {
    (void)arg;

    /*
     * 此时已经处于内核态,并且 CR3 已经切换到了目标进程的页表 (由 arch_thread_switch 完成)
     * 只需要构造栈帧并跳转到用户态
     */

    pr_info("Jumping to user mode at 0x%x, SP=0x%x", USER_CODE_BASE, USER_STACK_TOP);

    /* 跳转到用户模式 */
    enter_user_mode(USER_CODE_BASE, USER_STACK_TOP);

    /* 永远不会返回 */
    panic("Returned from user mode!");
}
