/**
 * @file apic.h
 * @brief x86 APIC 寄存器定义和接口
 *
 * 包含 Local APIC 和 I/O APIC 的寄存器定义
 */

#ifndef ASM_X86_APIC_H
#define ASM_X86_APIC_H

#include <arch/mmu.h>

#include <xnix/types.h>

/* LAPIC 默认物理地址 */
#define LAPIC_BASE_DEFAULT 0xFEE00000

/* IA32_APIC_BASE MSR */
#define MSR_IA32_APIC_BASE  0x1B
#define APIC_BASE_BSP       (1 << 8)  /* Bootstrap Processor */
#define APIC_BASE_X2APIC    (1 << 10) /* x2APIC 模式 */
#define APIC_BASE_ENABLE    (1 << 11) /* APIC 全局使能 */
#define APIC_BASE_ADDR_MASK 0xFFFFF000

/* LAPIC 寄存器偏移 */
#define LAPIC_ID        0x020 /* Local APIC ID */
#define LAPIC_VER       0x030 /* Local APIC Version */
#define LAPIC_TPR       0x080 /* Task Priority Register */
#define LAPIC_APR       0x090 /* Arbitration Priority Register */
#define LAPIC_PPR       0x0A0 /* Processor Priority Register */
#define LAPIC_EOI       0x0B0 /* End of Interrupt */
#define LAPIC_RRD       0x0C0 /* Remote Read Register */
#define LAPIC_LDR       0x0D0 /* Logical Destination Register */
#define LAPIC_DFR       0x0E0 /* Destination Format Register */
#define LAPIC_SVR       0x0F0 /* Spurious Interrupt Vector Register */
#define LAPIC_ISR       0x100 /* In-Service Register (8 regs) */
#define LAPIC_TMR       0x180 /* Trigger Mode Register (8 regs) */
#define LAPIC_IRR       0x200 /* Interrupt Request Register (8 regs) */
#define LAPIC_ESR       0x280 /* Error Status Register */
#define LAPIC_CMCI      0x2F0 /* LVT CMCI Register */
#define LAPIC_ICR_LO    0x300 /* Interrupt Command Register (low 32-bit) */
#define LAPIC_ICR_HI    0x310 /* Interrupt Command Register (high 32-bit) */
#define LAPIC_LVT_TIMER 0x320 /* LVT Timer Register */
#define LAPIC_LVT_THERM 0x330 /* LVT Thermal Sensor Register */
#define LAPIC_LVT_PERF  0x340 /* LVT Performance Monitoring Register */
#define LAPIC_LVT_LINT0 0x350 /* LVT LINT0 Register */
#define LAPIC_LVT_LINT1 0x360 /* LVT LINT1 Register */
#define LAPIC_LVT_ERR   0x370 /* LVT Error Register */
#define LAPIC_TIMER_ICR 0x380 /* Timer Initial Count Register */
#define LAPIC_TIMER_CCR 0x390 /* Timer Current Count Register */
#define LAPIC_TIMER_DCR 0x3E0 /* Timer Divide Configuration Register */

/* SVR 寄存器位 */
#define LAPIC_SVR_ENABLE (1 << 8)

/* ICR Delivery Mode */
#define ICR_FIXED   (0 << 8)
#define ICR_LOWEST  (1 << 8)
#define ICR_SMI     (2 << 8)
#define ICR_NMI     (4 << 8)
#define ICR_INIT    (5 << 8)
#define ICR_STARTUP (6 << 8)

/* ICR Destination Mode */
#define ICR_PHYSICAL (0 << 11)
#define ICR_LOGICAL  (1 << 11)

/* ICR Delivery Status */
#define ICR_IDLE         (0 << 12)
#define ICR_SEND_PENDING (1 << 12)

/* ICR Level */
#define ICR_DEASSERT (0 << 14)
#define ICR_ASSERT   (1 << 14)

/* ICR Trigger Mode */
#define ICR_EDGE  (0 << 15)
#define ICR_LEVEL (1 << 15)

/* ICR Destination Shorthand */
#define ICR_NO_SHORTHAND (0 << 18)
#define ICR_SELF         (1 << 18)
#define ICR_ALL_INC_SELF (2 << 18)
#define ICR_ALL_EXC_SELF (3 << 18)

/* LVT Timer 模式 */
#define LVT_TIMER_ONESHOT  (0 << 17)
#define LVT_TIMER_PERIODIC (1 << 17)
#define LVT_TIMER_TSC_DL   (2 << 17) /* TSC-Deadline Mode */

/* LVT 屏蔽位 */
#define LVT_MASKED (1 << 16)

/* Timer Divide 配置 */
#define TIMER_DIV_1   0x0B
#define TIMER_DIV_2   0x00
#define TIMER_DIV_4   0x01
#define TIMER_DIV_8   0x02
#define TIMER_DIV_16  0x03
#define TIMER_DIV_32  0x08
#define TIMER_DIV_64  0x09
#define TIMER_DIV_128 0x0A

/* I/O APIC 默认物理地址 */
#define IOAPIC_BASE_DEFAULT 0xFEC00000

/* I/O APIC 寄存器选择 */
#define IOAPIC_REGSEL 0x00
#define IOAPIC_REGWIN 0x10

/* I/O APIC 寄存器索引 */
#define IOAPIC_ID     0x00
#define IOAPIC_VER    0x01
#define IOAPIC_ARB    0x02
#define IOAPIC_REDTBL 0x10 /* 重定向表, 每个 IRQ 占 2 个 32 位寄存器 */

/* I/O APIC 重定向表项标志 */
#define IOAPIC_INT_MASKED    (1 << 16)
#define IOAPIC_TRIGGER_LEVEL (1 << 15)
#define IOAPIC_ACTIVE_LOW    (1 << 13)
#define IOAPIC_DEST_LOGICAL  (1 << 11)

/* IPI 向量定义 */
#define IPI_VECTOR_RESCHED 0xF0
#define IPI_VECTOR_TLB     0xF1
#define IPI_VECTOR_PANIC   0xF2

/* LAPIC 接口 */
void     lapic_init(void);
void     lapic_eoi(void);
uint8_t  lapic_get_id(void);
void     lapic_send_ipi(uint8_t lapic_id, uint8_t vector);
void     lapic_send_ipi_all(uint8_t vector);
void     lapic_send_init(uint8_t lapic_id);
void     lapic_send_init_deassert(void);
void     lapic_send_sipi(uint8_t lapic_id, uint8_t vector);
void     lapic_timer_init(uint32_t freq);
void     lapic_timer_stop(void);
uint32_t lapic_read(uint32_t reg);
void     lapic_write(uint32_t reg, uint32_t val);

/* I/O APIC 接口 */
void     ioapic_init(void);
void     ioapic_enable_irq(uint8_t irq, uint8_t vector, uint8_t dest);
void     ioapic_disable_irq(uint8_t irq);
void     ioapic_set_base(paddr_t base);
uint32_t ioapic_read(uint8_t reg);
void     ioapic_write(uint8_t reg, uint32_t val);

/* APIC 芯片注册 */
void apic_register(void);

#endif /* ASM_X86_APIC_H */
