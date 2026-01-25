/**
 * @file page_alloc.c
 * @brief Bitmap 物理页分配器
 *
 * 用一个 bit 数组记录每个物理页帧的状态:0 = 空闲,1 = 已分配
 * bitmap 本身放在可用内存的开头,大小根据实际内存动态决定
 */

#include <arch/mmu.h>

#include <xnix/mm.h>
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

void page_alloc_init(void) {
    paddr_t raw_start, raw_end;
    arch_get_memory_range(&raw_start, &raw_end);

    raw_start = PAGE_ALIGN_UP(raw_start);
    raw_end   = PAGE_ALIGN_DOWN(raw_end);

    /* 先算总共有多少页(包括 bitmap 要占用的) */
    uint32_t raw_pages = (raw_end - raw_start) / PAGE_SIZE;

    /* bitmap 需要多少字节?每页 1 bit */
    uint32_t bitmap_bytes = (raw_pages + 7) / 8;
    uint32_t bitmap_pages = PAGE_ALIGN_UP(bitmap_bytes) / PAGE_SIZE;

    /* bitmap 放在内存开头 */
    page_bitmap = (uint32_t *)raw_start;
    bitmap_size = bitmap_bytes / 4;

    /* 可分配内存从 bitmap 之后开始 */
    memory_start   = raw_start + bitmap_pages * PAGE_SIZE;
    memory_end     = raw_end;
    total_pages    = (memory_end - memory_start) / PAGE_SIZE;
    now_free_pages = total_pages;

    /* 清零 bitmap */
    memset(page_bitmap, 0, bitmap_bytes);

    kprintf("Page allocator: %u pages (%u KB), bitmap %u pages at 0x%x\n", total_pages,
            total_pages * 4, bitmap_pages, (uint32_t)page_bitmap);
}

void *alloc_page(void) {
    uint32_t flags = spin_lock_irqsave(&page_lock);

    for (uint32_t i = 0; i < total_pages; i++) {
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

    for (uint32_t i = 0; i < total_pages; i++) {
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
        kprintf("free_page: invalid address 0x%x\n", addr);
        return;
    }
    if (addr % PAGE_SIZE != 0) {
        kprintf("free_page: unaligned address 0x%x\n", addr);
        return;
    }

    uint32_t pfn   = (addr - memory_start) / PAGE_SIZE;
    uint32_t flags = spin_lock_irqsave(&page_lock);

    if (!bitmap_test(pfn)) {
        spin_unlock_irqrestore(&page_lock, flags);
        kprintf("free_page: double free at 0x%x\n", addr);
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
