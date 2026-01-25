/**
 * @file policy_rr.c
 * @brief Round-Robin 轮转调度策略
 *
 * 简单的时间片轮转:
 * - 每个线程固定时间片
 * - 用完时间片或主动 yield 后,移到队尾
 * - FIFO 队列,公平轮转
 */

#include <arch/smp.h>

#include <kernel/sched/sched.h>

/* 时间片长度(tick 数) */
#define RR_TIMESLICE 10

/* 获取运行队列(单核简化版) */
extern struct runqueue *sched_get_runqueue(cpu_id_t cpu);

/*
 * 队列操作
 **/

static void rr_enqueue(struct thread *t, cpu_id_t cpu) {
    struct runqueue *rq = sched_get_runqueue(cpu);

    t->next       = NULL;
    t->state      = THREAD_READY;
    t->time_slice = RR_TIMESLICE; /* 重置时间片 */

    if (!rq->head) {
        /* 队列为空 */
        rq->head = t;
        rq->tail = t;
    } else {
        /* 加入队尾 */
        rq->tail->next = t;
        rq->tail       = t;
    }

    rq->nr_running++;
}

static void rr_dequeue(struct thread *t) {
    /* 遍历队列找到 t 并移除 */
    cpu_id_t cpu = t->running_on;
    if (cpu == CPU_ID_INVALID) {
        cpu = 0; /* 默认 CPU */
    }

    struct runqueue *rq   = sched_get_runqueue(cpu);
    struct thread   *prev = NULL;
    struct thread   *curr = rq->head;

    while (curr) {
        if (curr == t) {
            if (prev) {
                prev->next = curr->next;
            } else {
                rq->head = curr->next;
            }

            if (rq->tail == curr) {
                rq->tail = prev;
            }

            rq->nr_running--;
            t->next = NULL;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

static struct thread *rr_pick_next(void) {
    cpu_id_t         cpu = cpu_current_id();
    struct runqueue *rq  = sched_get_runqueue(cpu);

    if (!rq->head) {
        return NULL;
    }

    /* 如果队列头的线程时间片为 0,轮转到队尾 */
    if (rq->head->time_slice == 0) {
        struct thread *t = rq->head;

        /* 从队列头移除 */
        rq->head = t->next;
        if (!rq->head) {
            rq->tail = NULL;
        }

        /* 加到队尾(会在 enqueue 中重置时间片) */
        t->next = NULL;
        if (rq->tail) {
            rq->tail->next = t;
            rq->tail       = t;
        } else {
            rq->head = t;
            rq->tail = t;
        }

        /* 重置时间片 */
        t->time_slice = RR_TIMESLICE;
    }

    return rq->head;
}

static bool rr_tick(struct thread *current) {
    if (!current || current->time_slice == 0) {
        return false;
    }

    current->time_slice--;

    /* 时间片用完,触发调度 */
    if (current->time_slice == 0) {
        return true;
    }

    return false;
}

static cpu_id_t rr_select_cpu(struct thread *t) {
    /* 简单负载均衡:选负载最低的 CPU */
    cpu_id_t best_cpu = 0;
    uint32_t min_load = sched_get_runqueue(0)->nr_running;

    for (cpu_id_t cpu = 0; cpu < cpu_count(); cpu++) {
        if (!CPUS_TEST(t->cpus_workable, cpu)) {
            continue; /* 线程不能在这个 CPU 运行 */
        }

        uint32_t load = sched_get_runqueue(cpu)->nr_running;
        if (load < min_load) {
            min_load = load;
            best_cpu = cpu;
        }
    }

    return best_cpu;
}

static void rr_init(void) {
    /* 无需初始化 */
}

/*
 * 策略导出
 **/

struct sched_policy sched_policy_rr = {
    .name       = "round-robin",
    .init       = rr_init,
    .enqueue    = rr_enqueue,
    .dequeue    = rr_dequeue,
    .pick_next  = rr_pick_next,
    .tick       = rr_tick,
    .select_cpu = rr_select_cpu,
};
