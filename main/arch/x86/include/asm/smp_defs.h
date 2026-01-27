/**
 * @file smp_defs.h
 * @brief x86 SMP 相关定义
 *
 * Per-CPU 数据结构和 MP Table 相关定义
 */

#ifndef ASM_X86_SMP_DEFS_H
#define ASM_X86_SMP_DEFS_H

#include <xnix/types.h>
#include <xnix/config.h>
#include <arch/mmu.h> /* for paddr_t */

/* 前向声明 */
struct thread;
struct tss_entry;

/**
 * MP 浮点结构签名 "_MP_"
 */
#define MP_FPS_SIGNATURE 0x5F504D5F

/**
 * MP 配置表签名 "PCMP"
 */
#define MP_CFG_SIGNATURE 0x504D4350

/**
 * MP 浮点结构
 *
 * 位于 BIOS 区域,用于定位 MP 配置表
 */
struct mp_fps {
    uint32_t signature;        /* "_MP_" */
    uint32_t config_ptr;       /* MP 配置表物理地址 */
    uint8_t  length;           /* 结构长度 (以 16 字节为单位) */
    uint8_t  spec_rev;         /* MP 规范版本 */
    uint8_t  checksum;         /* 校验和 */
    uint8_t  features[5];      /* 特性字节 */
} __attribute__((packed));

/**
 * MP 配置表头
 */
struct mp_config {
    uint32_t signature;        /* "PCMP" */
    uint16_t length;           /* 表长度 */
    uint8_t  spec_rev;         /* 规范版本 */
    uint8_t  checksum;         /* 校验和 */
    char     oem_id[8];        /* OEM 标识 */
    char     product_id[12];   /* 产品标识 */
    uint32_t oem_table;        /* OEM 表指针 */
    uint16_t oem_table_size;   /* OEM 表大小 */
    uint16_t entry_count;      /* 配置表条目数 */
    uint32_t lapic_addr;       /* LAPIC 基地址 */
    uint16_t ext_table_len;    /* 扩展表长度 */
    uint8_t  ext_checksum;     /* 扩展表校验和 */
    uint8_t  reserved;
} __attribute__((packed));

/* MP 配置表条目类型 */
#define MP_ENTRY_PROCESSOR 0
#define MP_ENTRY_BUS       1
#define MP_ENTRY_IOAPIC    2
#define MP_ENTRY_IOINT     3
#define MP_ENTRY_LINT      4

/**
 * 处理器条目
 */
struct mp_processor {
    uint8_t  type;             /* 0 = 处理器 */
    uint8_t  lapic_id;         /* Local APIC ID */
    uint8_t  lapic_ver;        /* LAPIC 版本 */
    uint8_t  flags;            /* CPU 标志 */
    uint32_t signature;        /* CPU 签名 */
    uint32_t features;         /* 特性标志 */
    uint32_t reserved[2];
} __attribute__((packed));

/* 处理器标志 */
#define MP_PROC_ENABLED  0x01  /* CPU 可用 */
#define MP_PROC_BSP      0x02  /* 是 BSP */

/**
 * 总线条目
 */
struct mp_bus {
    uint8_t type;              /* 1 = 总线 */
    uint8_t bus_id;            /* 总线 ID */
    char    bus_type[6];       /* 总线类型字符串 */
} __attribute__((packed));

/**
 * I/O APIC 条目
 */
struct mp_ioapic {
    uint8_t  type;             /* 2 = I/O APIC */
    uint8_t  id;               /* I/O APIC ID */
    uint8_t  version;          /* 版本 */
    uint8_t  flags;            /* 标志 */
    uint32_t addr;             /* I/O APIC 基地址 */
} __attribute__((packed));

/* I/O APIC 标志 */
#define MP_IOAPIC_ENABLED 0x01

/**
 * I/O 中断条目
 */
struct mp_ioint {
    uint8_t type;              /* 3 = I/O 中断 */
    uint8_t int_type;          /* 中断类型 */
    uint16_t flags;            /* 极性/触发模式 */
    uint8_t src_bus;           /* 源总线 */
    uint8_t src_irq;           /* 源 IRQ */
    uint8_t dst_apic;          /* 目标 APIC ID */
    uint8_t dst_intin;         /* INTIN# */
} __attribute__((packed));

/**
 * 本地中断条目
 */
struct mp_lint {
    uint8_t type;              /* 4 = 本地中断 */
    uint8_t int_type;          /* 中断类型 */
    uint16_t flags;            /* 极性/触发模式 */
    uint8_t src_bus;           /* 源总线 */
    uint8_t src_irq;           /* 源 IRQ */
    uint8_t dst_lapic;         /* 目标 LAPIC ID (0xFF = 所有) */
    uint8_t dst_lintin;        /* LINTIN# */
} __attribute__((packed));

/**
 * Per-CPU 数据结构
 */
struct per_cpu_data {
    uint32_t         cpu_id;       /* 逻辑 CPU ID (0 = BSP) */
    uint8_t          lapic_id;     /* 硬件 LAPIC ID */
    struct thread   *idle_thread;  /* idle 线程 */
    struct thread   *current;      /* 当前运行线程 */
    uint32_t        *int_stack;    /* 中断栈顶 */
    struct tss_entry*tss;          /* TSS 指针 */
    volatile bool    started;      /* AP 已启动 */
    volatile bool    ready;        /* AP 就绪 */
    uint32_t         timer_ticks;  /* 本 CPU 定时器 tick 计数 */
} __attribute__((aligned(64)));    /* 缓存行对齐 */

/**
 * SMP 信息结构 (由 MP Table 解析填充)
 */
struct smp_info {
    uint32_t cpu_count;                        /* CPU 总数 */
    uint32_t bsp_id;                           /* BSP 的逻辑 ID */
    uint8_t  lapic_ids[CFG_MAX_CPUS];          /* 每个 CPU 的 LAPIC ID */
    paddr_t  lapic_base;                       /* LAPIC 基地址 */
    paddr_t  ioapic_base;                      /* I/O APIC 基地址 */
    uint8_t  ioapic_id;                        /* I/O APIC ID */
    bool     apic_available;                   /* APIC 是否可用 */
};

/* MP Table 解析接口 */
int  mp_table_parse(struct smp_info *info);
void mp_table_dump(const struct smp_info *info);

/* Per-CPU 数据访问 */
extern struct per_cpu_data g_per_cpu[CFG_MAX_CPUS];

/**
 * 获取当前 CPU 的 Per-CPU 数据
 */
static inline struct per_cpu_data *get_cpu_data(void) {
    /* 通过 LAPIC ID 查找对应的 per_cpu 数据 */
    extern uint8_t lapic_get_id(void);
    extern struct smp_info g_smp_info;

    uint8_t lapic_id = lapic_get_id();
    for (uint32_t i = 0; i < g_smp_info.cpu_count; i++) {
        if (g_smp_info.lapic_ids[i] == lapic_id) {
            return &g_per_cpu[i];
        }
    }
    return &g_per_cpu[0]; /* fallback to BSP */
}

#endif /* ASM_X86_SMP_DEFS_H */
