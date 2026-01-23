/**
 * @file atomic.c
 * @brief x86 原子操作实现
 *
 * 参考文档：
 *   Intel SDM Vol.2 指令参考:
 *     - LOCK 前缀: 锁定总线，保证原子性
 *     - XCHG: 交换，隐式 LOCK（不需要加前缀）
 *     - CMPXCHG: 比较交换，需要 LOCK 前缀
 *     - XADD: 交换并相加，需要 LOCK 前缀
 *   Intel SDM Vol.3A Ch.8 "Multiple-Processor Management":
 *     - 8.1 Locked Atomic Operations
 *     - 8.2 Memory Ordering (x86 是强内存序，大部分操作自带顺序保证)
 *   下载: https://www.intel.com/sdm
 *   OSDev: https://wiki.osdev.org/Spinlock
 */

#include <arch/atomic.h>

/*
 * 基本读写
 * x86 对齐的 32 位读写本身是原子的，volatile 防止编译器优化
 */
int32_t atomic_read(const atomic_t *v) {
    return v->value;
}

void atomic_set(atomic_t *v, int32_t val) {
    v->value = val;
}

/*
 * atomic_add - 原子加法
 * 使用 LOCK XADD: 交换 delta 和 *v，然后 *v += 原 delta
 * XADD 返回旧值，所以要 +delta 得到新值
 */
int32_t atomic_add(atomic_t *v, int32_t delta) {
    int32_t old = delta;
    __asm__ volatile("lock xaddl %0, %1" : "+r"(old), "+m"(v->value) : : "memory");
    return old + delta;
}

int32_t atomic_sub(atomic_t *v, int32_t delta) {
    return atomic_add(v, -delta);
}

int32_t atomic_inc(atomic_t *v) {
    return atomic_add(v, 1);
}

int32_t atomic_dec(atomic_t *v) {
    return atomic_add(v, -1);
}

/*
 * atomic_xchg - 原子交换
 * XCHG 指令隐式带 LOCK 语义，无需加前缀
 */
int32_t atomic_xchg(atomic_t *v, int32_t new) {
    int32_t old = new;
    __asm__ volatile("xchgl %0, %1" : "+r"(old), "+m"(v->value) : : "memory");
    return old;
}

/*
 * atomic_cmpxchg - 比较并交换 (CAS)
 * CMPXCHG: if (eax == *dst) { *dst = src; ZF=1; } else { eax = *dst; ZF=0; }
 * 返回是否交换成功
 */
bool atomic_cmpxchg(atomic_t *v, int32_t old, int32_t new) {
    bool success;
    __asm__ volatile("lock cmpxchgl %3, %1\n\t"
                     "sete %0"
                     : "=q"(success), "+m"(v->value), "+a"(old)
                     : "r"(new)
                     : "memory", "cc");
    return success;
}

/*
 * 内存屏障
 * x86 是强内存序，大部分情况不需要显式屏障
 * 但多核场景下仍需要保证可见性
 *
 * MFENCE: 全屏障，序列化所有 load/store
 * LFENCE: 读屏障，序列化 load
 * SFENCE: 写屏障，序列化 store
 *
 * 注: 486 没有这些指令，用 lock 前缀的空操作代替
 */
void barrier_full(void) {
    __asm__ volatile("mfence" ::: "memory");
}

void barrier_read(void) {
    __asm__ volatile("lfence" ::: "memory");
}

void barrier_write(void) {
    __asm__ volatile("sfence" ::: "memory");
}
