/**
 * @file kmsg.c
 * @brief 内核日志环形缓冲实现
 *
 * 使用定长环形缓冲存储日志条目.当缓冲满时,丢弃最旧的条目.
 * 每条日志由 kmsg_entry 头 + 文本 + NUL 组成.
 */

#include <drivers/timer.h>

#include <xnix/kmsg.h>
#include <xnix/stdio.h>
#include <xnix/string.h>
#include <xnix/sync.h>

/* 环形缓冲 */
static char     kmsg_buf[KMSG_BUF_SIZE];
static uint32_t kmsg_head = 0; /* 下一条写入位置 */
static uint32_t kmsg_tail = 0; /* 最旧条目位置 */
static uint32_t kmsg_seq  = 0; /* 下一条写入的序列号 */

/* 已被覆盖的最小序列号(低于此值的条目已丢失) */
static uint32_t kmsg_first_seq = 0;

static spinlock_t kmsg_lock = SPINLOCK_INIT;

static bool kmsg_initialized = false;

/* 缓冲区已使用字节数 */
static inline uint32_t kmsg_used(void) {
    if (kmsg_head >= kmsg_tail) {
        return kmsg_head - kmsg_tail;
    }
    return KMSG_BUF_SIZE - kmsg_tail + kmsg_head;
}

/* 从环形缓冲中读取 n 个字节到 dst,从 offset 位置开始 */
static void kmsg_buf_read(uint32_t offset, void *dst, uint32_t n) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < n; i++) {
        d[i] = kmsg_buf[(offset + i) % KMSG_BUF_SIZE];
    }
}

/* 向环形缓冲写入 n 个字节 */
static void kmsg_buf_write(const void *src, uint32_t n) {
    const uint8_t *s = src;
    for (uint32_t i = 0; i < n; i++) {
        kmsg_buf[(kmsg_head + i) % KMSG_BUF_SIZE] = s[i];
    }
    kmsg_head = (kmsg_head + n) % KMSG_BUF_SIZE;
}

/* 丢弃 tail 处最旧的一条日志 */
static void kmsg_discard_oldest(void) {
    struct kmsg_entry hdr;
    kmsg_buf_read(kmsg_tail, &hdr, sizeof(hdr));
    uint32_t entry_size = sizeof(hdr) + hdr.len + 1;
    kmsg_tail           = (kmsg_tail + entry_size) % KMSG_BUF_SIZE;
    kmsg_first_seq      = hdr.seq + 1;
}

/* 计算一条新条目需要的总空间 */
static inline uint32_t entry_total_size(uint16_t text_len) {
    return sizeof(struct kmsg_entry) + text_len + 1; /* +1 for NUL */
}

void kmsg_init(void) {
    kmsg_head        = 0;
    kmsg_tail        = 0;
    kmsg_seq         = 0;
    kmsg_first_seq   = 0;
    kmsg_initialized = true;
}

void kmsg_log_raw(int level, int facility, const char *text, uint16_t len) {
    if (!kmsg_initialized) {
        return;
    }

    if (len > KMSG_MAX_LINE) {
        len = KMSG_MAX_LINE;
    }

    uint32_t need = entry_total_size(len);

    /* 缓冲区必须能放下至少一条(保证不死循环) */
    if (need > KMSG_BUF_SIZE - 1) {
        return;
    }

    uint32_t flags = spin_lock_irqsave(&kmsg_lock);

    /* 腾出空间:丢弃最旧的条目直到有足够空间 */
    while (KMSG_BUF_SIZE - kmsg_used() - 1 < need && kmsg_tail != kmsg_head) {
        kmsg_discard_oldest();
    }

    /* 构造条目头 */
    struct kmsg_entry hdr;
    hdr.seq       = kmsg_seq++;
    hdr.timestamp = (uint32_t)timer_get_ticks();
    hdr.level     = (uint8_t)level;
    hdr.facility  = (uint8_t)facility;
    hdr.len       = len;

    /* 写入头 */
    kmsg_buf_write(&hdr, sizeof(hdr));

    /* 写入文本 */
    kmsg_buf_write(text, len);

    /* 写入 NUL 终止符 */
    char nul = '\0';
    kmsg_buf_write(&nul, 1);

    spin_unlock_irqrestore(&kmsg_lock, flags);
}

void kmsg_log(int level, int facility, const char *fmt, ...) {
    char              text[KMSG_MAX_LINE];
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    /* 使用 vsnprintf 格式化(需要内核提供) */
    int len = 0;
    /* 手动格式化:逐字符处理 */
    const char *p = fmt;
    while (*p && len < KMSG_MAX_LINE - 1) {
        if (*p != '%') {
            text[len++] = *p++;
            continue;
        }
        p++;
        switch (*p) {
        case 's': {
            const char *s = __builtin_va_arg(args, const char *);
            if (!s) {
                s = "(null)";
            }
            while (*s && len < KMSG_MAX_LINE - 1) {
                text[len++] = *s++;
            }
            break;
        }
        case 'd': {
            int32_t v = __builtin_va_arg(args, int32_t);
            char    num[16];
            int     nlen = snprintf(num, sizeof(num), "%d", v);
            for (int i = 0; i < nlen && len < KMSG_MAX_LINE - 1; i++) {
                text[len++] = num[i];
            }
            break;
        }
        case 'u': {
            uint32_t v = __builtin_va_arg(args, uint32_t);
            char     num[16];
            int      nlen = snprintf(num, sizeof(num), "%u", v);
            for (int i = 0; i < nlen && len < KMSG_MAX_LINE - 1; i++) {
                text[len++] = num[i];
            }
            break;
        }
        case 'x': {
            uint32_t v = __builtin_va_arg(args, uint32_t);
            char     num[16];
            int      nlen = snprintf(num, sizeof(num), "%x", v);
            for (int i = 0; i < nlen && len < KMSG_MAX_LINE - 1; i++) {
                text[len++] = num[i];
            }
            break;
        }
        case 'p': {
            void *v = __builtin_va_arg(args, void *);
            char  num[16];
            int   nlen = snprintf(num, sizeof(num), "0x%08x", (uint32_t)(uintptr_t)v);
            for (int i = 0; i < nlen && len < KMSG_MAX_LINE - 1; i++) {
                text[len++] = num[i];
            }
            break;
        }
        case '%':
            text[len++] = '%';
            break;
        case 'c': {
            char c      = (char)__builtin_va_arg(args, int);
            text[len++] = c;
            break;
        }
        default:
            if (len < KMSG_MAX_LINE - 2) {
                text[len++] = '%';
                text[len++] = *p;
            }
            break;
        }
        if (*p) {
            p++;
        }
    }

    __builtin_va_end(args);
    text[len] = '\0';

    kmsg_log_raw(level, facility, text, (uint16_t)len);
}

int kmsg_read(uint32_t *seq, char *buf, size_t size) {
    if (!kmsg_initialized || !seq || !buf || size == 0) {
        return -1;
    }

    uint32_t flags = spin_lock_irqsave(&kmsg_lock);

    /* 请求的 seq 已被覆盖,跳到最旧的可用条目 */
    if (*seq < kmsg_first_seq) {
        *seq = kmsg_first_seq;
    }

    /* 没有更多条目 */
    if (*seq >= kmsg_seq) {
        spin_unlock_irqrestore(&kmsg_lock, flags);
        return -1;
    }

    /* 找到对应 seq 的条目:从 tail 开始线性扫描 */
    uint32_t offset  = kmsg_tail;
    uint32_t cur_seq = kmsg_first_seq;

    while (cur_seq < *seq) {
        struct kmsg_entry hdr;
        kmsg_buf_read(offset, &hdr, sizeof(hdr));
        offset = (offset + entry_total_size(hdr.len)) % KMSG_BUF_SIZE;
        cur_seq++;
    }

    /* 读取条目头 */
    struct kmsg_entry hdr;
    kmsg_buf_read(offset, &hdr, sizeof(hdr));

    /* 格式化输出: "<level>,<seq>,<timestamp>;<text>\n" */
    char header_str[64];
    int  hdr_len =
        snprintf(header_str, sizeof(header_str), "%u,%u,%u;", hdr.level, hdr.seq, hdr.timestamp);
    uint32_t total = (uint32_t)hdr_len + hdr.len + 1; /* +1 for \n */

    if (total >= size) {
        spin_unlock_irqrestore(&kmsg_lock, flags);
        return -2; /* 缓冲区太小 */
    }

    /* 复制 header */
    memcpy(buf, header_str, hdr_len);

    /* 复制文本 */
    uint32_t text_offset = (offset + sizeof(hdr)) % KMSG_BUF_SIZE;
    kmsg_buf_read(text_offset, buf + hdr_len, hdr.len);

    /* 添加换行和 NUL */
    buf[hdr_len + hdr.len]     = '\n';
    buf[hdr_len + hdr.len + 1] = '\0';

    /* 更新序列号 */
    *seq = hdr.seq + 1;

    spin_unlock_irqrestore(&kmsg_lock, flags);
    return (int)(hdr_len + hdr.len + 1);
}

uint32_t kmsg_get_seq(void) {
    return kmsg_seq;
}
