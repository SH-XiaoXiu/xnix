/**
 * @file spawn.c
 * @brief 进程加载和启动
 */

#include <arch/cpu.h>

#include <kernel/capability/capability.h>
#include <kernel/process/process.h>
#include <kernel/sched/sched.h>
#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h>
#include <xnix/string.h>

/* 声明 vmm_kmap/kunmap */
extern void *vmm_kmap(paddr_t paddr);
extern void  vmm_kunmap(void *vaddr);

/* 声明架构相关的用户态跳转函数 */
extern void enter_user_mode(uint32_t eip, uint32_t esp);

/* 声明内部函数 */
extern thread_t thread_create_with_owner(const char *name, void (*entry)(void *), void *arg,
                                         struct process *owner);

/* 用户栈配置 */
#define USER_STACK_TOP 0xBFFFF000

/*
 * 用户线程入口
 * 当线程第一次被调度时,会从这里开始执行
 *
 * 需要在用户栈上设置 argc=0, argv=NULL 以兼容新的 CRT0
 */
void user_thread_entry(void *arg) {
    /*
     * 此时已经处于内核态,并且 CR3 已经切换到了目标进程的页表
     * 需要在用户栈上设置 argc/argv,然后跳转到用户态
     *
     * 栈布局:
     *   [esp+0] = argc (0)
     *   [esp+4] = argv (NULL)
     */
    struct process *proc = process_get_current();
    if (!proc) {
        panic("No current process in user_thread_entry");
    }

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        panic("No mm_ops in user_thread_entry");
    }

    /* 栈顶在 USER_STACK_TOP,需要设置 argc/argv(共 8 字节,16 字节对齐) */
    uint32_t final_esp       = (USER_STACK_TOP - 16) & ~15;
    uint32_t esp_page_vaddr  = final_esp & ~(PAGE_SIZE - 1);
    uint32_t esp_page_offset = final_esp & (PAGE_SIZE - 1);
    paddr_t  esp_paddr       = (paddr_t)mm->query(proc->page_dir_phys, esp_page_vaddr);
    if (esp_paddr) {
        void     *mapped = vmm_kmap(esp_paddr);
        uint32_t *stack  = (uint32_t *)((uint8_t *)mapped + esp_page_offset);
        stack[0]         = 0; /* argc = 0 */
        stack[1]         = 0; /* argv = NULL */
        vmm_kunmap(mapped);
    }

    enter_user_mode((uint32_t)arg, final_esp);

    /* 永远不会返回 */
    panic("Returned from user mode!");
}

pid_t process_spawn_init(void *elf_data, uint32_t elf_size) {
    return process_spawn_module("init", elf_data, elf_size);
}

pid_t process_spawn_module(const char *name, void *elf_data, uint32_t elf_size) {
    return process_spawn_module_ex(name, elf_data, elf_size, NULL, 0);
}

pid_t process_spawn_module_ex(const char *name, void *elf_data, uint32_t elf_size,
                              const struct spawn_inherit_cap *inherit_caps,
                              uint32_t                        inherit_count) {
    struct process *proc = (struct process *)process_create(name);
    if (!proc) {
        pr_err("Failed to create process");
        return PID_INVALID;
    }

    struct process *creator = process_get_current();

    /* 设置父子关系 */
    proc->parent = creator;
    if (creator) {
        uint32_t flags = cpu_irq_save();
        spin_lock(&process_list_lock);
        proc->next_sibling = creator->children;
        creator->children  = proc;
        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);

        /* 继承父进程的 cwd */
        strncpy(proc->cwd, creator->cwd, PROCESS_CWD_MAX - 1);
        proc->cwd[PROCESS_CWD_MAX - 1] = '\0';
    }
    for (uint32_t i = 0; i < inherit_count; i++) {
        cap_handle_t dup =
            cap_duplicate_to(creator, inherit_caps[i].src, proc, inherit_caps[i].rights);
        if (dup == CAP_HANDLE_INVALID) {
            pr_err("Failed to inherit capability for %s", name ? name : "?");
            process_destroy((process_t)proc);
            return PID_INVALID;
        }
        if (inherit_caps[i].expected_dst != CAP_HANDLE_INVALID &&
            dup != inherit_caps[i].expected_dst) {
            pr_warn("Boot: inherited handle mismatch (%u -> %u)", inherit_caps[i].expected_dst,
                    dup);
        }
    }

    int      ret;
    uint32_t entry_point = 0;
    if (elf_data) {
        ret = process_load_elf(proc, elf_data, elf_size, &entry_point);
    } else {
        pr_err("No module provided");
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    if (ret < 0) {
        pr_err("Failed to load program: %d", ret);
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    thread_t t =
        thread_create_with_owner("bootstrap", user_thread_entry, (void *)entry_point, proc);
    if (!t) {
        pr_err("Failed to create process thread");
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    process_add_thread(proc, (struct thread *)t);
    pr_ok("Spawned %s process (PID %d)", name ? name : "?", proc->pid);
    return proc->pid;
}

/**
 * 用户线程入口(带 argv)
 * arg 指向 argv_info 结构
 */
struct argv_info {
    uint32_t entry_point;
    uint32_t stack_top;
};

static void user_thread_entry_with_args(void *arg) {
    struct argv_info *info = (struct argv_info *)arg;
    enter_user_mode(info->entry_point, info->stack_top);
    panic("Returned from user mode!");
}

pid_t process_spawn_elf_with_args(const char *name, void *elf_data, uint32_t elf_size, int argc,
                                  char argv[][ABI_EXEC_MAX_ARG_LEN]) {
    struct process *proc = (struct process *)process_create(name);
    if (!proc) {
        pr_err("Failed to create process");
        return PID_INVALID;
    }

    struct process *creator = process_get_current();

    /* 设置父子关系 */
    proc->parent = creator;
    if (creator) {
        uint32_t flags = cpu_irq_save();
        spin_lock(&process_list_lock);
        proc->next_sibling = creator->children;
        creator->children  = proc;
        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);

        /* 继承父进程的 cwd */
        strncpy(proc->cwd, creator->cwd, PROCESS_CWD_MAX - 1);
        proc->cwd[PROCESS_CWD_MAX - 1] = '\0';
    }

    int      ret;
    uint32_t entry_point = 0;
    if (elf_data) {
        ret = process_load_elf(proc, elf_data, elf_size, &entry_point);
    } else {
        pr_err("No ELF data provided");
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    if (ret < 0) {
        pr_err("Failed to load ELF: %d", ret);
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    /* 设置 argv 到用户栈 */
    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    /*
     * 用户栈布局(从高地址到低地址):
     *
     *   高地址
     *   +------------------+
     *   | argv 字符串      |  "arg0\0" "arg1\0" ...
     *   +------------------+
     *   | padding (对齐)   |
     *   +------------------+
     *   | NULL             |  argv[argc]
     *   +------------------+
     *   | argv[argc-1]     |  指向 "arg(argc-1)"
     *   | ...              |
     *   | argv[0]          |  指向 "arg0"
     *   +------------------+
     *   | argv 指针        |  指向 argv[0]
     *   +------------------+
     *   | argc             |
     *   +------------------+  <- ESP
     *   低地址
     */

    uint32_t stack_top = USER_STACK_TOP;

    /* 计算字符串总长度 */
    uint32_t strings_size = 0;
    for (int i = 0; i < argc; i++) {
        strings_size += strlen(argv[i]) + 1;
    }

    /* 在栈顶放置字符串 */
    uint32_t strings_start = stack_top - strings_size;
    strings_start &= ~3; /* 4 字节对齐 */

    /* argv 数组(包含 NULL 结尾) */
    uint32_t argv_array_size = (argc + 1) * sizeof(uint32_t);
    uint32_t argv_array_addr = strings_start - argv_array_size;
    argv_array_addr &= ~3;

    /* argc 和 argv 指针 */
    uint32_t final_esp = argv_array_addr - 8; /* argc + argv ptr */
    final_esp &= ~15;                         /* 16 字节对齐 */

    /* 写入字符串和 argv 数组 */
    uint32_t str_offset = strings_start;
    for (int i = 0; i < argc; i++) {
        uint32_t len = strlen(argv[i]) + 1;

        /* 查找字符串所在的物理页并写入 */
        uint32_t page_vaddr  = str_offset & ~(PAGE_SIZE - 1);
        uint32_t page_offset = str_offset & (PAGE_SIZE - 1);
        paddr_t  paddr       = (paddr_t)mm->query(proc->page_dir_phys, page_vaddr);
        if (!paddr) {
            pr_err("Stack page not mapped for argv strings");
            process_destroy((process_t)proc);
            return PID_INVALID;
        }

        void *mapped = vmm_kmap(paddr);
        memcpy((uint8_t *)mapped + page_offset, argv[i], len);
        vmm_kunmap(mapped);

        /* 写入 argv[i] 指针 */
        uint32_t argv_entry_addr  = argv_array_addr + i * sizeof(uint32_t);
        uint32_t argv_page_vaddr  = argv_entry_addr & ~(PAGE_SIZE - 1);
        uint32_t argv_page_offset = argv_entry_addr & (PAGE_SIZE - 1);
        paddr_t  argv_paddr       = (paddr_t)mm->query(proc->page_dir_phys, argv_page_vaddr);
        if (!argv_paddr) {
            pr_err("Stack page not mapped for argv array");
            process_destroy((process_t)proc);
            return PID_INVALID;
        }

        mapped                                              = vmm_kmap(argv_paddr);
        *(uint32_t *)((uint8_t *)mapped + argv_page_offset) = str_offset;
        vmm_kunmap(mapped);

        str_offset += len;
    }

    /* 写入 argv[argc] = NULL */
    uint32_t null_addr        = argv_array_addr + argc * sizeof(uint32_t);
    uint32_t null_page_vaddr  = null_addr & ~(PAGE_SIZE - 1);
    uint32_t null_page_offset = null_addr & (PAGE_SIZE - 1);
    paddr_t  null_paddr       = (paddr_t)mm->query(proc->page_dir_phys, null_page_vaddr);
    if (null_paddr) {
        void *mapped                                        = vmm_kmap(null_paddr);
        *(uint32_t *)((uint8_t *)mapped + null_page_offset) = 0;
        vmm_kunmap(mapped);
    }

    /* 写入 argc 和 argv 指针到 final_esp 位置 */
    uint32_t esp_page_vaddr  = final_esp & ~(PAGE_SIZE - 1);
    uint32_t esp_page_offset = final_esp & (PAGE_SIZE - 1);
    paddr_t  esp_paddr       = (paddr_t)mm->query(proc->page_dir_phys, esp_page_vaddr);
    if (esp_paddr) {
        void     *mapped = vmm_kmap(esp_paddr);
        uint32_t *stack  = (uint32_t *)((uint8_t *)mapped + esp_page_offset);
        stack[0]         = (uint32_t)argc;  /* argc */
        stack[1]         = argv_array_addr; /* argv */
        vmm_kunmap(mapped);
    }

    /* 分配 argv_info 结构(需要在内核堆中分配,线程结束后释放) */
    struct argv_info *info = kmalloc(sizeof(struct argv_info));
    if (!info) {
        process_destroy((process_t)proc);
        return PID_INVALID;
    }
    info->entry_point = entry_point;
    info->stack_top   = final_esp;

    thread_t t = thread_create_with_owner("bootstrap", user_thread_entry_with_args, info, proc);
    if (!t) {
        pr_err("Failed to create process thread");
        kfree(info);
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    process_add_thread(proc, (struct thread *)t);
    pr_debug("Spawned %s process (PID %d) with %d args", name ? name : "?", proc->pid, argc);
    return proc->pid;
}
