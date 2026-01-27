/**
 * @file process.h
 * @brief 进程 API
 *
 * 进程是资源的容器,包含地址空间,能力表,线程等.
 */

#ifndef XNIX_PROCESS_H
#define XNIX_PROCESS_H

#include <xnix/abi/types.h>
#include <xnix/types.h>

/**
 * 进程句柄
 *
 * 用访问器获取信息:process_get_pid()
 */
typedef struct process *process_t;

/* pid_t 和 PID_INVALID 来自 <xnix/abi/types.h> */

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

/**
 * 创建并启动用户进程
 * @param elf_data ELF 文件数据指针 (NULL 表示使用内置加载器)
 * @param elf_size ELF 文件大小
 */
pid_t process_spawn_init(void *elf_data, uint32_t elf_size);

/**
 * 加载 ELF 文件到进程
 */
int process_load_elf(struct process *proc, void *elf_data, uint32_t elf_size, uint32_t *out_entry);

#endif
