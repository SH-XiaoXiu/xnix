/**
 * @file kernel/sys/sys_fb.c
 * @brief Framebuffer 系统调用实现
 */

#include <kernel/process/process.h>
#include <kernel/sys/syscall.h>
#include <xnix/abi/framebuffer.h>
#include <xnix/boot.h>
#include <xnix/errno.h>
#include <xnix/mm.h>
#include <xnix/mm_ops.h>
#include <xnix/stdio.h>
#include <xnix/syscall.h>
#include <xnix/usraccess.h>
#include <xnix/vmm.h>

/**
 * SYS_FB_INFO: 获取 framebuffer 信息
 *
 * @param args[0] info 用户空间 abi_fb_info 指针
 * @return 0 成功,-1 无 framebuffer,-EFAULT 地址错误
 */
static int32_t sys_fb_info(const uint32_t *args) {
    struct abi_fb_info *user_info = (struct abi_fb_info *)args[0];

    if (!user_info) {
        return -EINVAL;
    }

    struct boot_framebuffer_info boot_info;
    if (boot_get_framebuffer(&boot_info) < 0) {
        return -ENODEV;
    }

    /* 转换为 ABI 结构 */
    struct abi_fb_info info = {
        .width      = boot_info.width,
        .height     = boot_info.height,
        .pitch      = boot_info.pitch,
        .bpp        = boot_info.bpp,
        .red_pos    = boot_info.red_pos,
        .red_size   = boot_info.red_size,
        .green_pos  = boot_info.green_pos,
        .green_size = boot_info.green_size,
        .blue_pos   = boot_info.blue_pos,
        .blue_size  = boot_info.blue_size,
    };

    if (copy_to_user(user_info, &info, sizeof(info)) != 0) {
        return -EFAULT;
    }

    return 0;
}

/* 跟踪是否已映射,避免重复映射 */
static bool fb_mapped_for_pid[64] = {0};

/**
 * SYS_FB_MAP: 映射 framebuffer 到用户空间
 *
 * @return 用户空间映射地址,失败返回 -1
 */
static int32_t sys_fb_map(const uint32_t *args) {
    (void)args;

    struct process *proc = process_get_current();
    if (!proc) {
        return -1;
    }

    /* 检查是否已映射 */
    pid_t pid = proc->pid;
    if (pid >= 0 && pid < 64 && fb_mapped_for_pid[pid]) {
        /* 已映射,直接返回地址 */
        return (int32_t)ABI_FB_MAP_BASE;
    }

    struct boot_framebuffer_info info;
    if (boot_get_framebuffer(&info) < 0) {
        return -ENODEV;
    }

    /* 计算 framebuffer 大小 */
    uint32_t fb_size = info.pitch * info.height;
    uint32_t fb_phys = (uint32_t)info.addr;

    /* 页对齐 */
    uint32_t fb_phys_start = fb_phys & ~(PAGE_SIZE - 1);
    uint32_t fb_offset     = fb_phys - fb_phys_start;
    uint32_t fb_pages      = (fb_size + fb_offset + PAGE_SIZE - 1) / PAGE_SIZE;

    /* 用户空间映射基地址 */
    uint32_t user_base = ABI_FB_MAP_BASE;

    const struct mm_operations *mm = mm_get_ops();
    if (!mm || !mm->map) {
        return -ENODEV;
    }

    /* 映射所有 framebuffer 页面 */
    for (uint32_t i = 0; i < fb_pages; i++) {
        uint32_t vaddr = user_base + i * PAGE_SIZE;
        uint32_t paddr = fb_phys_start + i * PAGE_SIZE;

        /*
         * 不使用 NOCACHE,与内核 fb 映射保持一致.
         * Write-back 缓存对于大量像素写入性能更好.
         */
        int ret = mm->map(proc->page_dir_phys, vaddr, paddr,
                          VMM_PROT_USER | VMM_PROT_READ | VMM_PROT_WRITE);
        if (ret != 0) {
            /* 回滚已映射的页面 */
            for (uint32_t j = 0; j < i; j++) {
                mm->unmap(proc->page_dir_phys, user_base + j * PAGE_SIZE);
            }
            return -ENOMEM;
        }
    }

    /* 标记已映射 */
    if (pid >= 0 && pid < 64) {
        fb_mapped_for_pid[pid] = true;
    }

    /* 返回用户空间地址(包含页内偏移) */
    return (int32_t)(user_base + fb_offset);
}

/**
 * 注册 framebuffer 系统调用
 */
void sys_fb_init(void) {
    syscall_register(SYS_FB_INFO, sys_fb_info, 1, "fb_info");
    syscall_register(SYS_FB_MAP, sys_fb_map, 0, "fb_map");
}
