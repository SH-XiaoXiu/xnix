/**
 * @file malloc.c
 * @brief 用户态内存分配器
 *
 * 基于 sbrk 的简单 first-fit 分配器.
 * 使用空闲块链表管理已释放的内存.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <xnix/syscall.h>

/* 块头部大小对齐 */
#define ALIGN       8
#define ALIGN_UP(x) (((x) + (ALIGN - 1)) & ~(ALIGN - 1))

/* 最小分配单元(避免碎片) */
#define MIN_ALLOC 16

/* 块头部 */
struct block_header {
    size_t               size;  /* 数据区大小(不含头部) */
    struct block_header *next;  /* 空闲链表指针(仅空闲块使用) */
    uint32_t             magic; /* 魔数,用于检测 double-free */
};

#define HEADER_SIZE ALIGN_UP(sizeof(struct block_header))
#define MAGIC_ALLOC 0xA110CA7E /* "ALLOCATE" */
#define MAGIC_FREE  0xF2EEB10C /* "FREEBLOC" */

/* 空闲链表头 */
static struct block_header *free_list = NULL;

/* 从系统获取更多内存 */
static struct block_header *request_memory(size_t size) {
    size_t total = HEADER_SIZE + size;

    /* 至少请求 4KB,减少系统调用 */
    if (total < 4096) {
        total = 4096;
    }

    void *ptr = sys_sbrk((int32_t)total);
    if (ptr == (void *)-1) {
        return NULL;
    }

    struct block_header *block = (struct block_header *)ptr;
    block->size                = total - HEADER_SIZE;
    block->next                = NULL;
    block->magic               = MAGIC_FREE;

    return block;
}

/* 将块加入空闲链表(按地址排序,便于合并) */
static void add_to_free_list(struct block_header *block) {
    block->magic = MAGIC_FREE;

    if (!free_list || block < free_list) {
        block->next = free_list;
        free_list   = block;
        return;
    }

    struct block_header *prev = free_list;
    while (prev->next && prev->next < block) {
        prev = prev->next;
    }
    block->next = prev->next;
    prev->next  = block;
}

/* 尝试合并相邻的空闲块 */
static void coalesce(void) {
    struct block_header *curr = free_list;
    while (curr && curr->next) {
        /* 检查是否相邻 */
        char *curr_end = (char *)curr + HEADER_SIZE + curr->size;
        if (curr_end == (char *)curr->next) {
            /* 合并 */
            curr->size += HEADER_SIZE + curr->next->size;
            curr->next = curr->next->next;
            /* 继续检查是否还能合并 */
        } else {
            curr = curr->next;
        }
    }
}

void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    /* 对齐请求大小 */
    size = ALIGN_UP(size);
    if (size < MIN_ALLOC) {
        size = MIN_ALLOC;
    }

    /* first-fit 搜索空闲链表 */
    struct block_header *prev  = NULL;
    struct block_header *block = free_list;

    while (block) {
        if (block->size >= size) {
            /* 找到合适的块 */
            /* 如果剩余空间足够,拆分块 */
            if (block->size >= size + HEADER_SIZE + MIN_ALLOC) {
                struct block_header *new_block =
                    (struct block_header *)((char *)block + HEADER_SIZE + size);
                new_block->size  = block->size - size - HEADER_SIZE;
                new_block->next  = block->next;
                new_block->magic = MAGIC_FREE;

                block->size = size;
                block->next = new_block;
            }

            /* 从空闲链表移除 */
            if (prev) {
                prev->next = block->next;
            } else {
                free_list = block->next;
            }

            block->next  = NULL;
            block->magic = MAGIC_ALLOC;
            return (char *)block + HEADER_SIZE;
        }

        prev  = block;
        block = block->next;
    }

    /* 没有合适的空闲块,请求更多内存 */
    block = request_memory(size);
    if (!block) {
        return NULL;
    }

    /* 如果请求了更多内存,可能需要拆分 */
    if (block->size >= size + HEADER_SIZE + MIN_ALLOC) {
        struct block_header *new_block =
            (struct block_header *)((char *)block + HEADER_SIZE + size);
        new_block->size  = block->size - size - HEADER_SIZE;
        new_block->next  = NULL;
        new_block->magic = MAGIC_FREE;

        block->size = size;
        add_to_free_list(new_block);
    }

    block->magic = MAGIC_ALLOC;
    return (char *)block + HEADER_SIZE;
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }

    struct block_header *block = (struct block_header *)((char *)ptr - HEADER_SIZE);

    /* 检查魔数 */
    if (block->magic == MAGIC_FREE) {
        /* double-free 检测 */
        return;
    }
    if (block->magic != MAGIC_ALLOC) {
        /* 损坏的块或无效指针 */
        return;
    }

    add_to_free_list(block);
    coalesce();
}

void *calloc(size_t nmemb, size_t size) {
    /* 溢出检查 */
    if (nmemb && size > (size_t)-1 / nmemb) {
        return NULL;
    }

    size_t total = nmemb * size;
    void  *ptr   = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    struct block_header *block = (struct block_header *)((char *)ptr - HEADER_SIZE);
    if (block->magic != MAGIC_ALLOC) {
        return NULL;
    }

    /* 如果当前块足够大,直接返回 */
    size = ALIGN_UP(size);
    if (block->size >= size) {
        return ptr;
    }

    /* 分配新块,复制数据,释放旧块 */
    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    memcpy(new_ptr, ptr, block->size);
    free(ptr);
    return new_ptr;
}
