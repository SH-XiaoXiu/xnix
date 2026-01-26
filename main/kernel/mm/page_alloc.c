/**
 * @file page_alloc.c
 * @brief Bitmap 物理页分配器
 *
 * 用一个 bit 数组记录每个物理页帧的状态:0 = 空闲,1 = 已分配
 * bitmap 本身放在可用内存的开头,大小根据实际内存动态决定
 */

#include <arch/mmu.h>

#include <xnix/debug.h>
#include <xnix/mm.h>
#include <xnix/config.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/sync.h>

/* bitmap 指针,指向物理内存开头 */
static uint32_t *page_bitmap;
static uint32_t  bitmap_size; /* bitmap 占用的 uint32_t 数量 */
static uint32_t  total_pages; /* 可分配的页数(不含 bitmap 占用的) */
static uint32_t  now_free_pages;
static paddr_t   memory_start; /* 可分配内存起始(bitmap 之后) */
static paddr_t   memory_end;
static uint32_t  bitmap_bytes;
static uint32_t  low_pages;

static spinlock_t page_lock = SPINLOCK_INIT;

static inline void bitmap_set(uint32_t pfn) {
    page_bitmap[pfn / 32] |= (1U << (pfn % 32));
}

static inline void bitmap_clear(uint32_t pfn) {
    page_bitmap[pfn / 32] &= ~(1U << (pfn % 32));
}

static inline bool bitmap_test(uint32_t pfn) {
    return (page_bitmap[pfn / 32] & (1U << (pfn % 32))) != 0;
}

static void bitmap_set_all(void) {
    memset(page_bitmap, 0xFF, bitmap_bytes);
}

static void bitmap_clear_range(uint32_t start_pfn, uint32_t end_pfn) {
    if (start_pfn >= end_pfn || end_pfn > total_pages) {
        return;
    }

    uint32_t i = start_pfn;

    while ((i < end_pfn) && (i & 31u)) {
        bitmap_clear(i);
        i++;
    }

    uint32_t word_start = i / 32u;
    uint32_t word_end   = end_pfn / 32u;
    if (word_end > word_start) {
        memset(&page_bitmap[word_start], 0, (size_t)(word_end - word_start) * sizeof(uint32_t));
    }

    i = word_end * 32u;
    while (i < end_pfn) {
        bitmap_clear(i);
        i++;
    }
}

void page_alloc_init(void) {
    paddr_t raw_start, raw_end;
    arch_get_memory_range(&raw_start, &raw_end);

    raw_start = PAGE_ALIGN_UP(raw_start);
    raw_end   = PAGE_ALIGN_DOWN(raw_end);

    /* 先算总共有多少页(包括 bitmap 要占用的) */
    uint32_t raw_pages = (raw_end - raw_start) / PAGE_SIZE;

    /* bitmap 需要多少字节?每页 1 bit */
    bitmap_bytes          = (raw_pages + 7) / 8;
    uint32_t bitmap_pages = PAGE_ALIGN_UP(bitmap_bytes) / PAGE_SIZE;

    /* bitmap 放在内存开头 */
    page_bitmap = (uint32_t *)raw_start;
    bitmap_size = (bitmap_bytes + 3) / 4;

    /* 可分配内存从 bitmap 之后开始 */
    memory_start   = raw_start + bitmap_pages * PAGE_SIZE;
    memory_end     = raw_end;
    total_pages    = (memory_end - memory_start) / PAGE_SIZE;
    now_free_pages = total_pages;
    low_pages      = total_pages;

    paddr_t low_end = (paddr_t)CFG_KERNEL_IDMAP_MB * 1024u * 1024u;
    if (low_end > memory_start && low_end < memory_end) {
        low_pages = (low_end - memory_start) / PAGE_SIZE;
    }

    bitmap_set_all();

    struct arch_mem_region regions[64];
    uint32_t               count = arch_get_memory_map(regions, 64);
    if (count) {
        now_free_pages = 0;
        for (uint32_t r = 0; r < count; r++) {
            if (regions[r].type != ARCH_MEM_USABLE) {
                continue;
            }
            paddr_t s = regions[r].start;
            paddr_t e = regions[r].end;
            if (e <= memory_start || s >= memory_end) {
                continue;
            }
            if (s < memory_start) {
                s = memory_start;
            }
            if (e > memory_end) {
                e = memory_end;
            }
            s = PAGE_ALIGN_UP(s);
            e = PAGE_ALIGN_DOWN(e);
            if (e <= s) {
                continue;
            }

            uint32_t start_pfn = (s - memory_start) / PAGE_SIZE;
            uint32_t end_pfn   = (e - memory_start) / PAGE_SIZE;
            bitmap_clear_range(start_pfn, end_pfn);
            now_free_pages += (end_pfn - start_pfn);
        }
    } else {
        memset(page_bitmap, 0, bitmap_bytes);
        now_free_pages = total_pages;
    }

    pr_info("Page allocator: %u pages (%u KB), bitmap %u pages at 0x%x", total_pages,
            total_pages * 4, bitmap_pages, (uint32_t)page_bitmap);
}

void *alloc_page(void) {
    uint32_t flags = spin_lock_irqsave(&page_lock);

    for (uint32_t i = 0; i < low_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            now_free_pages--;
            spin_unlock_irqrestore(&page_lock, flags);
            return (void *)(memory_start + i * PAGE_SIZE);
        }
    }

    spin_unlock_irqrestore(&page_lock, flags);
    return NULL;
}

void *alloc_page_high(void) {
    uint32_t flags = spin_lock_irqsave(&page_lock);

    for (uint32_t i = low_pages; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            now_free_pages--;
            spin_unlock_irqrestore(&page_lock, flags);
            return (void *)(memory_start + i * PAGE_SIZE);
        }
    }

    spin_unlock_irqrestore(&page_lock, flags);
    return NULL;
}

void *alloc_pages(uint32_t count) {
    if (count == 0) {
        return NULL;
    }
    if (count == 1) {
        return alloc_page();
    }

    uint32_t flags       = spin_lock_irqsave(&page_lock);
    uint32_t consecutive = 0;
    uint32_t start_pfn   = 0;

    for (uint32_t i = 0; i < low_pages; i++) {
        if (!bitmap_test(i)) {
            if (consecutive == 0) {
                start_pfn = i;
            }
            consecutive++;
            if (consecutive == count) {
                for (uint32_t j = start_pfn; j < start_pfn + count; j++) {
                    bitmap_set(j);
                }
                now_free_pages -= count;
                spin_unlock_irqrestore(&page_lock, flags);
                return (void *)(memory_start + start_pfn * PAGE_SIZE);
            }
        } else {
            consecutive = 0;
        }
    }

    spin_unlock_irqrestore(&page_lock, flags);
    return NULL;
}

void free_page(void *page) {
    if (!page) {
        return;
    }

    paddr_t addr = (paddr_t)page;
    if (addr < memory_start || addr >= memory_end) {
        pr_err("free_page: invalid address 0x%x", addr);
        return;
    }
    if (addr % PAGE_SIZE != 0) {
        pr_err("free_page: unaligned address 0x%x", addr);
        return;
    }

    uint32_t pfn   = (addr - memory_start) / PAGE_SIZE;
    uint32_t flags = spin_lock_irqsave(&page_lock);

    if (!bitmap_test(pfn)) {
        spin_unlock_irqrestore(&page_lock, flags);
        pr_err("free_page: double free at 0x%x", addr);
        return;
    }

    bitmap_clear(pfn);
    now_free_pages++;
    spin_unlock_irqrestore(&page_lock, flags);
}

void free_pages(void *page, uint32_t count) {
    if (!page || count == 0) {
        return;
    }
    paddr_t addr = (paddr_t)page;
    for (uint32_t i = 0; i < count; i++) {
        free_page((void *)(addr + i * PAGE_SIZE));
    }
}

uint32_t page_alloc_free_count(void) {
    return now_free_pages;
}

uint32_t page_alloc_total_count(void) {
    return total_pages;
}
