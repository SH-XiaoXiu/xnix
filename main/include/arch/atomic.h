/**
 * @file atomic.h
 * @brief 原子操作接口(平台无关)
 *
 * 原子操作 = 不可被中断的操作(对其他 CPU 和中断来说是瞬间完成的)
 *   普通的 i++ 实际上是 load → add → store 三步
 *   多核/中断场景下可能被打断,导致数据竞争
 *   原子操作保证这三步"打包"完成
 *
 * 各平台实现位于 arch/<platform>/atomic.c
 */

#ifndef ARCH_ATOMIC_H
#define ARCH_ATOMIC_H

#include <xnix/types.h>

/**
 * 原子整数类型
 * volatile 提示编译器不要优化,每次都从内存读取
 */
typedef struct {
    volatile int32_t value;
} atomic_t;

#define ATOMIC_INIT(v) {.value = (v)}

/* 基本读写 */
int32_t atomic_read(const atomic_t *v);
void    atomic_set(atomic_t *v, int32_t val);

/* 带内存序的读写 */
int32_t atomic_load_acquire(const atomic_t *v);         /* 读后屏障:后续操作不会重排到此读之前 */
void    atomic_store_release(atomic_t *v, int32_t val); /* 写前屏障:之前操作不会重排到此写之后 */

/* 算术操作 - 返回操作后的值 */
int32_t atomic_add(atomic_t *v, int32_t delta); /* v += delta, 返回新值 */
int32_t atomic_sub(atomic_t *v, int32_t delta); /* v -= delta, 返回新值 */
int32_t atomic_inc(atomic_t *v);                /* v++, 返回新值 */
int32_t atomic_dec(atomic_t *v);                /* v--, 返回新值 */

/**
 * Compare-And-Swap (CAS) 无锁编程的核心
 *
 * if (*v == old) { *v = new; return true; }
 * else { return false; }
 * 整个操作是原子的
 * 用于无锁数据结构,实现 spinlock (自旋锁)
 */
bool atomic_cmpxchg(atomic_t *v, int32_t old, int32_t new);

/**
 * Exchange - 原子交换
 *
 * tmp = *v; *v = new; return tmp;
 *
 * x86 的 xchg 指令自带 lock 语义,常用于实现 spinlock
 */
int32_t atomic_xchg(atomic_t *v, int32_t new);

/*
 * 内存屏障 (Memory Barrier / Fence)
 * CPU 和编译器会重排指令以优化性能
 * 屏障强制某些操作的顺序,保证多核间看到一致的内存状态
 * 读屏障:屏障前的读 happens-before 屏障后的读
 * 写屏障:屏障前的写 happens-before 屏障后的写
 * 全屏障:两者都有
 */
void barrier_read(void);  /* 读屏障 */
void barrier_write(void); /* 写屏障 */
void barrier_full(void);  /* 全屏障 */

/* 编译器屏障 - 只阻止编译器重排,不阻止 CPU 重排 */
#define BARRIER_COMPILER() __asm__ volatile("" ::: "memory")

#endif
