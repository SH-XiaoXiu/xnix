/**
 * @file process.h
 * @brief 进程 API
 *
 * 进程是资源的容器,包含地址空间,能力表,线程等.
 */

#ifndef XNIX_PROCESS_H
#define XNIX_PROCESS_H

#include <xnix/types.h>

/**
 * 进程句柄
 *
 * 用访问器获取信息:process_get_pid()
 */
typedef struct process *process_t;

typedef uint32_t pid_t;
#define PID_INVALID ((pid_t)-1)

/**
 * 进程状态
 */
typedef enum {
    PROCESS_RUNNING, /* 至少有一个线程在运行 */
    PROCESS_ZOMBIE,  /* 已退出,等待父进程回收 */
} process_state_t;

/**
 * 创建进程
 *
 * @param name 进程名,用于调试
 * @return 进程句柄,失败返回 NULL
 */
process_t process_create(const char *name);

/**
 * 销毁进程
 */
void process_destroy(process_t proc);

/**
 * 获取当前进程
 */
process_t process_current(void);

/**
 * 访问器
 */
pid_t           process_get_pid(process_t proc);
const char     *process_get_name(process_t proc);
process_state_t process_get_state(process_t proc);

/**
 * 进程管理初始化
 */
void process_init(void);

#endif
