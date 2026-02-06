/**
 * @file spawn.c
 * @brief 进程加载和启动
 */

#include "process_internal.h"

#include <arch/cpu.h>

#include <xnix/boot.h>
#include <xnix/debug.h>
#include <xnix/handle.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/perm.h>
#include <xnix/process_def.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/thread_def.h>
#include <xnix/vm_layout.h>

/* 声明 vmm_kmap/kunmap */
extern void *vmm_kmap(paddr_t paddr);
extern void  vmm_kunmap(void *vaddr);

/* 声明架构相关的用户态跳转函数 */
extern void enter_user_mode(uint32_t eip, uint32_t esp);

/* 声明内部函数 */
extern thread_t thread_create_with_owner(const char *name, void (*entry)(void *), void *arg,
                                         struct process *owner);

/* argv 传递信息 */
struct argv_info {
    uint32_t entry_point;
    uint32_t stack_top;
};

/**
 * 设置父子关系和继承 cwd
 */
static void spawn_setup_parent(struct process *proc, struct process *creator) {
    proc->parent = creator;
    if (creator) {
        uint32_t flags = cpu_irq_save();
        spin_lock(&process_list_lock);
        proc->next_sibling = creator->children;
        creator->children  = proc;
        spin_unlock(&process_list_lock);
        cpu_irq_restore(flags);
    }
}

/**
 * 传递 handles
 * @return 0 成功,-1 失败
 */
static int spawn_transfer_handles(struct process *proc, struct process *creator,
                                  const struct spawn_handle *handles, uint32_t handle_count) {
    pr_debug("[PROC] spawn: Transferring %u handles to %s\n", handle_count, proc->name);
    for (uint32_t i = 0; i < handle_count; i++) {
        pr_debug("[PROC] spawn:   %u: src=%u, name='%s'\n", i, handles[i].src, handles[i].name);
        handle_t dst =
            handle_transfer(creator, handles[i].src, proc, handles[i].name, HANDLE_INVALID);
        if (dst == HANDLE_INVALID) {
            pr_debug("[PROC] spawn: Failed to transfer handle %d (src=%u, name=%s)\n", i,
                     handles[i].src, handles[i].name);
            continue;
        }
        pr_debug("[PROC] spawn:   -> slot %u\n", dst);
    }
    return 0;
}

/**
 * 在用户栈上写入一个 uint32_t 值
 */
static int spawn_write_stack_u32(struct process *proc, const struct mm_operations *mm,
                                 uint32_t vaddr, uint32_t value) {
    uint32_t page_vaddr  = vaddr & ~(PAGE_SIZE - 1);
    uint32_t page_offset = vaddr & (PAGE_SIZE - 1);
    paddr_t  paddr       = (paddr_t)mm->query(proc->page_dir_phys, page_vaddr);
    if (!paddr) {
        return -1;
    }
    void *mapped                                   = vmm_kmap(paddr);
    *(uint32_t *)((uint8_t *)mapped + page_offset) = value;
    vmm_kunmap(mapped);
    return 0;
}

/**
 * 在用户栈上写入字符串
 */
static int spawn_write_stack_str(struct process *proc, const struct mm_operations *mm,
                                 uint32_t vaddr, const char *str, uint32_t len) {
    uint32_t page_vaddr  = vaddr & ~(PAGE_SIZE - 1);
    uint32_t page_offset = vaddr & (PAGE_SIZE - 1);
    paddr_t  paddr       = (paddr_t)mm->query(proc->page_dir_phys, page_vaddr);
    if (!paddr) {
        return -1;
    }
    void *mapped = vmm_kmap(paddr);
    memcpy((uint8_t *)mapped + page_offset, str, len);
    vmm_kunmap(mapped);
    return 0;
}

/**
 * 设置 argv 到用户栈
 * @return final_esp,失败返回 0
 */
static uint32_t spawn_setup_argv(struct process *proc, int argc,
                                 char argv[][ABI_EXEC_MAX_ARG_LEN]) {
    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        return 0;
    }

    uint32_t stack_top = USER_STACK_TOP;

    /* 计算字符串总长度 */
    uint32_t strings_size = 0;
    for (int i = 0; i < argc; i++) {
        strings_size += strlen(argv[i]) + 1;
    }

    /* 栈布局计算 */
    uint32_t strings_start   = (stack_top - strings_size) & ~3;
    uint32_t argv_array_size = (argc + 1) * sizeof(uint32_t);
    uint32_t argv_array_addr = (strings_start - argv_array_size) & ~3;
    uint32_t final_esp       = (argv_array_addr - 8) & ~15;

    /* 写入字符串和 argv 数组 */
    uint32_t str_offset = strings_start;
    for (int i = 0; i < argc; i++) {
        uint32_t len = strlen(argv[i]) + 1;

        if (spawn_write_stack_str(proc, mm, str_offset, argv[i], len) < 0) {
            pr_err("Stack page not mapped for argv strings");
            return 0;
        }

        if (spawn_write_stack_u32(proc, mm, argv_array_addr + i * sizeof(uint32_t), str_offset) <
            0) {
            pr_err("Stack page not mapped for argv array");
            return 0;
        }

        str_offset += len;
    }

    /* argv[argc] = NULL */
    spawn_write_stack_u32(proc, mm, argv_array_addr + argc * sizeof(uint32_t), 0);

    /* argc 和 argv 指针 */
    spawn_write_stack_u32(proc, mm, final_esp, (uint32_t)argc);
    spawn_write_stack_u32(proc, mm, final_esp + 4, argv_array_addr);

    return final_esp;
}

/**
 * 用户线程入口(无参数版本)
 */
void user_thread_entry(void *arg) {
    struct process *proc = process_get_current();
    if (!proc) {
        panic("No current process in user_thread_entry");
    }

    pr_debug("[PROC] start: process '%s' entry=0x%08x\n", proc->name, (uint32_t)arg);

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->query) {
        panic("No mm_ops in user_thread_entry");
    }

    /* 设置 argc=0, argv=NULL (直接写入,即使页面未映射也继续) */
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
    panic("Returned from user mode!");
}

/**
 * 用户线程入口(带参数版本)
 */
static void user_thread_entry_with_args(void *arg) {
    struct argv_info *info = (struct argv_info *)arg;
    enter_user_mode(info->entry_point, info->stack_top);
    panic("Returned from user mode!");
}

/**
 * 内部核心 spawn 函数
 */
static pid_t spawn_core(const char *name, void *elf_data, uint32_t elf_size,
                        const struct spawn_handle *handles, uint32_t handle_count,
                        struct perm_profile *profile, int argc, char argv[][ABI_EXEC_MAX_ARG_LEN]) {
    struct process *proc = (struct process *)process_create(name, profile);
    if (!proc) {
        pr_err("Failed to create process");
        return PID_INVALID;
    }

    struct process *creator = process_get_current();
    spawn_setup_parent(proc, creator);

    /* 传递 handles */
    if (handle_count > 0 && handles) {
        if (spawn_transfer_handles(proc, creator, handles, handle_count) < 0) {
            process_destroy((process_t)proc);
            return PID_INVALID;
        }
    }

    /* 对于 init 进程(由内核进程创建),直接创建 boot handles */
    /* 注意: process_get_current() 返回 kernel_process 而非 NULL */
    if (creator && creator->pid == 0) {
        boot_handles_create_for_init(proc);
    }

    /* 加载 ELF */
    if (!elf_data) {
        pr_err("No ELF data provided");
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    uint32_t entry_point = 0;
    int      ret         = process_load_elf(proc, elf_data, elf_size, &entry_point);
    if (ret < 0) {
        pr_err("Failed to load ELF: %d", ret);
        process_destroy((process_t)proc);
        return PID_INVALID;
    }

    /* 创建线程 */
    thread_t t;
    if (argc > 0 && argv) {
        uint32_t final_esp = spawn_setup_argv(proc, argc, argv);
        if (final_esp == 0) {
            process_destroy((process_t)proc);
            return PID_INVALID;
        }

        struct argv_info *info = kmalloc(sizeof(struct argv_info));
        if (!info) {
            process_destroy((process_t)proc);
            return PID_INVALID;
        }
        info->entry_point = entry_point;
        info->stack_top   = final_esp;

        t = thread_create_with_owner("bootstrap", user_thread_entry_with_args, info, proc);
        if (!t) {
            pr_err("Failed to create process thread");
            kfree(info);
            process_destroy((process_t)proc);
            return PID_INVALID;
        }
    } else {
        t = thread_create_with_owner("bootstrap", user_thread_entry, (void *)entry_point, proc);
        if (!t) {
            pr_err("Failed to create process thread");
            process_destroy((process_t)proc);
            return PID_INVALID;
        }
    }

    process_add_thread(proc, (struct thread *)t);
    pr_debug("[PROC] spawned: %s (PID %d)\n", name ? name : "?", proc->pid);
    return proc->pid;
}

/*
 * 公共 API
 */

pid_t process_spawn_init(void *elf_data, uint32_t elf_size) {
    struct perm_profile *profile = perm_profile_find("init");
    return spawn_core("init", elf_data, elf_size, NULL, 0, profile, 0, NULL);
}

pid_t process_spawn_module(const char *name, void *elf_data, uint32_t elf_size,
                           struct perm_profile *profile) {
    return spawn_core(name, elf_data, elf_size, NULL, 0, profile, 0, NULL);
}

pid_t process_spawn_module_ex(const char *name, void *elf_data, uint32_t elf_size,
                              const struct spawn_handle *handles, uint32_t handle_count,
                              struct perm_profile *profile) {
    return spawn_core(name, elf_data, elf_size, handles, handle_count, profile, 0, NULL);
}

pid_t process_spawn_module_ex_with_args(const char *name, void *elf_data, uint32_t elf_size,
                                        const struct spawn_handle *handles, uint32_t handle_count,
                                        struct perm_profile *profile, int argc,
                                        char argv[][ABI_EXEC_MAX_ARG_LEN]) {
    return spawn_core(name, elf_data, elf_size, handles, handle_count, profile, argc, argv);
}

pid_t process_spawn_elf_with_args(const char *name, void *elf_data, uint32_t elf_size, int argc,
                                  char argv[][ABI_EXEC_MAX_ARG_LEN], struct perm_profile *profile) {
    return spawn_core(name, elf_data, elf_size, NULL, 0, profile, argc, argv);
}

pid_t process_spawn_elf_ex_with_args(const char *name, void *elf_data, uint32_t elf_size,
                                     const struct spawn_handle *handles, uint32_t handle_count,
                                     struct perm_profile *profile, int argc,
                                     char argv[][ABI_EXEC_MAX_ARG_LEN]) {
    return spawn_core(name, elf_data, elf_size, handles, handle_count, profile, argc, argv);
}
