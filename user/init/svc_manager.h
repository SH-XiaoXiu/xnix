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

#define SVC_NAME_MAX        16
#define SVC_PATH_MAX        64
#define SVC_HANDLE_NAME_MAX 32
#define SVC_HANDLES_MAX     8
#define SVC_DEPS_MAX        8
#define SVC_MAX_SERVICES    16
#define SVC_MAX_HANDLE_DEFS 32
#define SVC_READY_DIR       "/run"

/* IPC 就绪通知消息 */
#define SVC_MSG_READY 0xF001

/**
 * 服务就绪通知消息
 */
struct svc_ready_msg {
    uint32_t magic;    /* SVC_MSG_READY */
    char     name[16]; /* 服务名 */
};

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
 * 依赖类型
 */
typedef enum {
    DEP_REQUIRES, /* 硬依赖:必须存在且就绪 */
    DEP_WANTS,    /* 软依赖:存在则等待,不存在也可启动 */
    DEP_AFTER,    /* 顺序依赖:仅保证启动顺序,不等就绪 */
} dep_type_t;

/**
 * 依赖边
 */
struct svc_dependency {
    int        target_idx;         /* 依赖的服务索引 */
    dep_type_t type;               /* 依赖类型 */
    char       name[SVC_NAME_MAX]; /* 依赖的服务名(用于验证) */
};

/**
 * 服务图节点
 */
struct svc_graph_node {
    /* 依赖关系 */
    struct svc_dependency deps[SVC_DEPS_MAX * 3];
    int                   dep_count;

    /* 拓扑排序相关 */
    int  topo_level;   /* 拓扑层级(0=无依赖,1=依赖0层,以此类推) */
    int  pending_deps; /* 尚未满足的依赖数 */
    bool visited;      /* 循环依赖检测标记 */
    bool in_path;      /* 当前路径标记(用于检测循环) */

    /* 服务发现 */
    char provides[SVC_DEPS_MAX][SVC_NAME_MAX]; /* 提供的 endpoint */
    int  provides_count;
    char requires[SVC_DEPS_MAX][SVC_NAME_MAX]; /* 需要的 endpoint */
    int  requires_count;
    char wants[SVC_DEPS_MAX][SVC_NAME_MAX]; /* 可选的 endpoint */
    int  wants_count;
};

/**
 * Handle 传递描述
 */
struct svc_handle_desc {
    char     name[SVC_HANDLE_NAME_MAX];
    uint32_t src_handle; /* 源 handle */
};

typedef enum {
    SVC_HANDLE_TYPE_NONE = 0,
    SVC_HANDLE_TYPE_ENDPOINT, /* 创建新的 IPC endpoint */
    SVC_HANDLE_TYPE_INHERIT,  /* 继承已存在的 handle(如 fb_mem) */
} svc_handle_type_t;

struct svc_handle_def {
    char              name[SVC_HANDLE_NAME_MAX];
    svc_handle_type_t type;
    bool              created;
    uint32_t          handle;
};

/**
 * 服务配置(从配置文件加载)
 */
struct svc_config {
    char name[SVC_NAME_MAX];

    bool       builtin;                   /* 是否为内置服务(已启动) */
    svc_type_t type;                      /* 启动类型 */
    char       module_name[SVC_NAME_MAX]; /* 模块名称(type=MODULE 时) */
    char       path[SVC_PATH_MAX];        /* ELF 路径(type=PATH 时) */

    /* 依赖声明 */
    char     after[SVC_DEPS_MAX][SVC_NAME_MAX]; /* 启动顺序依赖 */
    int      after_count;
    char     ready[SVC_DEPS_MAX][SVC_NAME_MAX]; /* 就绪等待依赖 */
    int      ready_count;
    char     wait_path[SVC_PATH_MAX]; /* 路径存在等待 */
    uint32_t delay_ms;                /* 启动前延时 */

    /* Profile */
    char profile[32];

    /* Permission overrides (格式: "xnix.io.port.0x3f8-0x3ff=true") */
    char perms[8][64]; /* 最多 8 个权限覆盖 */
    int  perm_count;

    /* Handle 传递 */
    struct svc_handle_desc handles[SVC_HANDLES_MAX];
    int                    handle_count;

    /* 挂载点 */
    char     mount[SVC_PATH_MAX]; /* 挂载路径(可选) */
    uint32_t mount_ep;            /* 挂载使用的 endpoint handle */

    /* 行为 */
    bool respawn;       /* 退出后自动重启 */
    bool no_ready_file; /* 不使用文件通知就绪状态 */
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
 * Permission profile (权限模板)
 */
#define SVC_MAX_PROFILES   8
#define SVC_PERM_NODES_MAX 32

struct svc_perm_entry {
    char name[64]; /* 权限节点名称,如 "xnix.ipc.send" */
    bool value;    /* true=允许, false=拒绝 */
};

struct svc_profile {
    char                  name[32];                  /* Profile 名称 */
    char                  inherit[32];               /* 继承的 profile */
    struct svc_perm_entry perms[SVC_PERM_NODES_MAX]; /* 权限列表 */
    int                   perm_count;
};

/**
 * 服务管理器
 */
struct svc_manager {
    struct svc_config  configs[SVC_MAX_SERVICES];
    struct svc_runtime runtime[SVC_MAX_SERVICES];

    /* 依赖图 */
    struct svc_graph_node graph[SVC_MAX_SERVICES];
    int                   topo_order[SVC_MAX_SERVICES]; /* 拓扑排序后的索引 */
    int                   max_topo_level;               /* 最大拓扑层级 */
    bool                  graph_valid;                  /* 图是否已验证 */

    struct svc_handle_def handle_defs[SVC_MAX_HANDLE_DEFS];
    int                   handle_def_count;
    int                   count;

    /* Permission profiles */
    struct svc_profile profiles[SVC_MAX_PROFILES];
    int                profile_count;
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
 * 解析 handles 字符串
 * 格式: "name name ..."
 *
 * @param mgr          管理器实例
 * @param handles_str  输入字符串
 * @param handles      输出数组
 * @param max_handles  最大 handle 数量
 * @return 解析出的 handle 数量
 */
int svc_parse_handles(struct svc_manager *mgr, const char *handles_str,
                      struct svc_handle_desc *handles, int max_handles);

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
 * 构建依赖图
 *
 * 从配置文件加载后调用,解析所有依赖关系并构建图
 *
 * @param mgr 管理器实例
 * @return 0 成功,负数失败
 */
int svc_build_dependency_graph(struct svc_manager *mgr);

/**
 * 解析服务的 provides/requires/wants 并自动分配 handles
 *
 * 在配置加载后,构建依赖图时调用
 *
 * @param mgr 管理器实例
 * @return 0 成功,负数失败
 */
int svc_resolve_service_discovery(struct svc_manager *mgr);

/**
 * 并行调度器 tick
 *
 * 按拓扑层级并行启动服务
 *
 * @param mgr 管理器实例
 */
void svc_tick_parallel(struct svc_manager *mgr);

/**
 * 处理服务就绪通知(IPC 消息)
 *
 * @param mgr 管理器实例
 * @param msg IPC 消息
 */
void svc_handle_ready_notification(struct svc_manager *mgr, struct ipc_message *msg);

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
