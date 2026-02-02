/**
 * @file svc_manager.h
 * @brief 声明式服务管理器
 *
 * 管理系统服务的配置加载,依赖解析,生命周期控制
 */

#ifndef SVC_MANAGER_H
#define SVC_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <xnix/syscall.h>

#define SVC_NAME_MAX     16
#define SVC_PATH_MAX     64
#define SVC_CAPS_MAX     4
#define SVC_DEPS_MAX     4
#define SVC_MAX_SERVICES 16
#define SVC_READY_DIR    "/run"

/**
 * 服务类型
 */
typedef enum {
    SVC_TYPE_MODULE, /* 模块索引方式启动 */
    SVC_TYPE_PATH,   /* ELF 路径方式启动(预留) */
} svc_type_t;

/**
 * 服务状态
 */
typedef enum {
    SVC_STATE_PENDING,  /* 等待条件满足 */
    SVC_STATE_WAITING,  /* 条件满足,等待延时 */
    SVC_STATE_STARTING, /* 正在启动 */
    SVC_STATE_RUNNING,  /* 运行中 */
    SVC_STATE_STOPPED,  /* 已停止 */
    SVC_STATE_FAILED,   /* 启动失败 */
} svc_state_t;

/**
 * Capability 传递描述
 */
struct svc_cap_desc {
    uint32_t src_handle; /* 源 handle */
    uint32_t rights;     /* 权限 */
    uint32_t dst_hint;   /* 目标 handle 提示 */
};

/**
 * 服务配置(从配置文件加载)
 */
struct svc_config {
    char name[SVC_NAME_MAX];

    svc_type_t type;               /* 启动类型 */
    uint32_t   module_index;       /* 模块索引(type=MODULE 时) */
    char       path[SVC_PATH_MAX]; /* ELF 路径(type=PATH 时) */

    /* 依赖声明 */
    char     after[SVC_DEPS_MAX][SVC_NAME_MAX]; /* 启动顺序依赖 */
    int      after_count;
    char     ready[SVC_DEPS_MAX][SVC_NAME_MAX]; /* 就绪等待依赖 */
    int      ready_count;
    char     wait_path[SVC_PATH_MAX]; /* 路径存在等待 */
    uint32_t delay_ms;                /* 启动前延时 */

    /* CAP 传递 */
    struct svc_cap_desc caps[SVC_CAPS_MAX];
    int                 cap_count;

    /* 挂载点 */
    char     mount[SVC_PATH_MAX]; /* 挂载路径(可选) */
    uint32_t mount_ep;            /* 挂载使用的 endpoint handle */

    /* 行为 */
    bool respawn; /* 退出后自动重启 */
};

/**
 * 服务运行时状态
 */
struct svc_runtime {
    svc_state_t state;
    int         pid;
    uint32_t    delay_start; /* delay 开始时间(ticks) */
    bool        ready;       /* 是否已报告就绪 */
};

/**
 * 服务管理器
 */
struct svc_manager {
    struct svc_config  configs[SVC_MAX_SERVICES];
    struct svc_runtime runtime[SVC_MAX_SERVICES];
    int                count;
};

/**
 * 初始化服务管理器
 *
 * @param mgr 管理器实例
 */
void svc_manager_init(struct svc_manager *mgr);

/**
 * 从配置文件加载服务
 *
 * @param mgr  管理器实例
 * @param path 配置文件路径
 * @return 0 成功,负数失败
 */
int svc_load_config(struct svc_manager *mgr, const char *path);

/**
 * 从字符串加载服务配置
 *
 * @param mgr     管理器实例
 * @param content 配置内容字符串
 * @return 0 成功,负数失败
 */
int svc_load_config_string(struct svc_manager *mgr, const char *content);

/**
 * 按名称查找服务索引
 *
 * @param mgr  管理器实例
 * @param name 服务名称
 * @return 服务索引,-1 未找到
 */
int svc_find_by_name(struct svc_manager *mgr, const char *name);

/**
 * 解析 caps 字符串
 * 格式: "cap_name:dst_hint cap_name:dst_hint ..."
 *
 * @param caps_str 输入字符串
 * @param caps     输出数组
 * @param max_caps 最大 cap 数量
 * @return 解析出的 cap 数量
 */
int svc_parse_caps(const char *caps_str, struct svc_cap_desc *caps, int max_caps);

/**
 * 检查服务是否可以启动
 *
 * @param mgr 管理器实例
 * @param idx 服务索引
 * @return true 可以启动
 */
bool svc_can_start(struct svc_manager *mgr, int idx);

/**
 * 启动服务
 *
 * @param mgr 管理器实例
 * @param idx 服务索引
 * @return 进程 ID,负数失败
 */
int svc_start_service(struct svc_manager *mgr, int idx);

/**
 * 服务管理器 tick,在主循环中调用
 *
 * 检查 ready 状态,尝试启动可启动的服务,处理延时
 *
 * @param mgr 管理器实例
 */
void svc_tick(struct svc_manager *mgr);

/**
 * 处理进程退出
 *
 * @param mgr    管理器实例
 * @param pid    退出的进程 ID
 * @param status 退出状态
 */
void svc_handle_exit(struct svc_manager *mgr, int pid, int status);

/**
 * 检查服务的 ready 文件是否存在
 *
 * @param name 服务名称
 * @return true 已就绪
 */
bool svc_check_ready_file(const char *name);

/**
 * 获取当前 tick 计数(毫秒)
 */
uint32_t svc_get_ticks(void);

#endif /* SVC_MANAGER_H */
