/**
 * @file mm.h
 * @brief 内核内存管理公共接口
 *
 * 内存管理分层：
 *   应用层: kmalloc / kfree 分配任意大小的内存
 *   页分配器: alloc_pages / free_pages ← kmalloc 内部用这个 以页 (4KB) 为单位分配
 *   物理内存: Bitmap / Buddy跟踪哪些页帧空闲 管理所有物理页帧
 *   - 页分配器：简单高效，适合大块内存（线程栈、页表）
 *   - kmalloc：灵活方便，适合小对象（结构体、缓冲区）
 *   - 不同场景用不同接口，性能和便利性兼顾
 */

#ifndef XNIX_MM_H
#define XNIX_MM_H

#include <arch/mmu.h>

#include <xnix/types.h>

/*
 * 页分配器 (Page Allocator)
 *
 * 最底层的内存分配，以页 (4KB) 为单位
 * 内部使用 Bitmap 跟踪每个页帧的状态
 */

/**
 * 分配一个物理页
 *
 * @return 页的起始地址，失败返回 NULL
 *
 * 使用示例：
 *   void *page = alloc_page();
 *   if (!page) panic("OOM");
 *   free_page(page);
 */
void *alloc_page(void);

/**
 * 分配连续的多个物理页
 *
 * @param count 需要的页数
 * @return 第一页的起始地址，失败返回 NULL
 *
 * 注意：连续分配开销比较大 尽量避免大量连续页的请求
 * 如果只需要虚拟地址连续（物理可以不连续）应该用 vmalloc（未实现）
 */
void *alloc_pages(uint32_t count);

/**
 * 释放一个物理页
 * @param page 之前 alloc_page 返回的地址
 */
void free_page(void *page);

/**
 * 释放连续的多个物理页
 * @param page  之前 alloc_pages 返回的地址
 * @param count 页数（必须和 alloc_pages 时一致）
 */
void free_pages(void *page, uint32_t count);

/*
 * 内核堆分配器 (Kernel Heap)
 *
 * 类似用户态的 malloc/free，分配任意大小的内存
 *
 * 当前实现：简单包装页分配器
 *   - 分配小于 PAGE_SIZE 浪费，但实现简单
 *   - 后续可优化为 Slab 分配器
 */

/**
 * 分配内核内存
 *
 * @param size 需要的字节数
 * @return 内存指针，失败返回 NULL
 *
 * 当前实现会向上取整到页大小：
 *   kmalloc(1)    → 实际分配 4096 字节
 *   kmalloc(5000) → 实际分配 8192 字节 (2 页)
 */
void *kmalloc(size_t size);

/**
 * 分配并清零
 * 等价于 kmalloc + memset(0)
 * 推荐用这个，避免使用未初始化内存
 */
void *kzalloc(size_t size);

/**
 * 释放内核内存
 *
 * @param ptr 之前 kmalloc/kzalloc 返回的指针
 *
 * 注意：
 *   - ptr 为 NULL 时安全（什么都不做）
 *   - 重复释放同一指针会导致未定义行为
 */
void kfree(void *ptr);

/*
 * 初始化和调试
 */

/**
 * 内存管理子系统初始化
 * 必须在使用任何内存分配函数前调用
 */
void mm_init(void);

/**
 * 打印内存使用统计
 * 调试用，输出类似：Memory: 1024 pages total, 512 free
 */
void mm_dump_stats(void);

/* 错误码 */
#define ENOMEM 12

#endif
