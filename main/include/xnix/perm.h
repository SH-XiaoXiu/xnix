/**
 * @file perm.h
 * @brief 权限 (Permission) 系统接口
 *
 * 权限系统基于 Capability 和 Role-Based Access Control (RBAC) 思想.
 * 支持细粒度的权限节点,权限继承和 Profile 管理.
 * 每个进程关联一个权限状态 (perm_state),决定了其可以执行的操作.
 */

#ifndef XNIX_PERM_H
#define XNIX_PERM_H

#include <xnix/sync.h>
#include <xnix/types.h>

/* 常用权限节点定义 */
#define PERM_NODE_IPC_SEND            "xnix.ipc.send"
#define PERM_NODE_IPC_RECV            "xnix.ipc.recv"
#define PERM_NODE_IPC_ENDPOINT_CREATE "xnix.ipc.endpoint.create"
#define PERM_NODE_HANDLE_GRANT        "xnix.handle.grant"
#define PERM_NODE_IO_PORT_ALL         "xnix.io.port.*"
#define PERM_NODE_MM_MMAP             "xnix.mm.mmap"
#define PERM_NODE_PROCESS_SPAWN       "xnix.process.spawn"
#define PERM_NODE_PROCESS_EXEC        "xnix.process.exec"

/**
 * @brief 权限 ID 类型
 *
 * 权限 ID 是权限节点的唯一数字标识,由注册表分配.
 * 使用 ID 而不是字符串可以加速运行时检查.
 */
typedef uint32_t perm_id_t;
#define PERM_ID_INVALID ((perm_id_t) - 1)

/**
 * @brief 权限值
 *
 * 三态逻辑:授予,拒绝,未定义.
 */
typedef enum {
    PERM_UNDEFINED = 0, /* 未定义(继承父级或默认拒绝) */
    PERM_DENY      = 1, /* 显式拒绝(优先级高于 GRANT) */
    PERM_GRANT     = 2, /* 显式授予 */
} perm_value_t;

/**
 * @brief 权限节点(全局注册表)
 *
 * 存储权限节点的元数据.
 */
struct perm_node {
    perm_id_t   id;    /* 唯一 ID */
    const char *name;  /* 驻留字符串(如 "xnix.ipc.send") */
    uint32_t    hash;  /* 预计算哈希,加速查找 */
    uint16_t    depth; /* 层级深度(用于优先级判断) */
};

/**
 * @brief 权限条目(在 profile 或覆盖列表中)
 */
struct perm_entry {
    const char  *node;  /* 节点名称(可能包含通配符,如 "xnix.ipc.*") */
    perm_value_t value; /* 权限值 */
};

/**
 * @brief 权限 Profile
 *
 * 类似角色的概念,包含一组权限规则.支持单继承.
 */
#define PERM_MAX_PROFILES 64
struct perm_profile {
    char                 name[32];      /* Profile 名称 */
    struct perm_profile *parent;        /* 父 Profile(继承其权限) */
    struct perm_entry   *perms;         /* 权限规则数组 */
    uint32_t             perm_count;    /* 规则数量 */
    uint32_t             perm_capacity; /* 规则容量 */
};

/**
 * @brief 进程权限状态
 *
 * 维护进程的当前权限快照.包含从 Profile 继承的权限和进程特有的覆盖.
 * 核心是 grant_bitmap,用于 O(1) 权限检查.
 */
struct perm_state {
    struct perm_profile *profile;   /* 关联的 Profile */
    struct perm_entry   *overrides; /* 进程级权限覆盖 */
    uint32_t             override_count;

    /* 解析后的权限位图 */
    uint32_t *grant_bitmap;            /* 1 bit 对应一个 perm_id,1表示有权限 */
    uint32_t  bitmap_words;            /* 位图大小(uint32_t 数量) */
    uint32_t  registry_count_snapshot; /* 最近一次解析时的权限节点数量 */

    /* 专用 I/O 端口位图(按需分配) */
    uint8_t *ioport_bitmap; /* 65536 bits = 8KB,控制 I/O 端口访问 */

    bool       dirty; /* 是否需要重新解析 */
    spinlock_t lock;  /* 状态锁 */
};

/* 注册表函数 */

/**
 * @brief 初始化权限子系统
 */
void perm_init(void);

/**
 * @brief 初始化权限注册表
 */
void perm_registry_init(void);

/**
 * @brief 注册权限节点
 *
 * @param name 权限节点名称(如 "xnix.ipc.send")
 * @return 分配的权限 ID
 */
perm_id_t perm_register(const char *name);

/**
 * @brief 查找权限 ID
 *
 * @param name 权限节点名称
 * @return 权限 ID,未找到返回 PERM_ID_INVALID
 */
perm_id_t perm_lookup(const char *name);

/**
 * @brief 获取权限节点名称
 *
 * @param id 权限 ID
 * @return 节点名称,ID 无效返回 NULL
 */
const char *perm_get_name(perm_id_t id);

/**
 * @brief 获取注册的权限节点总数
 *
 * @return 节点总数
 */
uint32_t perm_registry_count(void);

/* 状态函数 */

/**
 * @brief 创建权限状态
 *
 * @param profile 初始 Profile
 * @return 新创建的权限状态
 */
struct perm_state *perm_state_create(struct perm_profile *profile);

/**
 * @brief 销毁权限状态
 *
 * @param state 要销毁的状态
 */
void perm_state_destroy(struct perm_state *state);

/**
 * @brief 动态授予权限
 *
 * 添加一条 GRANT 规则到覆盖列表.
 *
 * @param state 目标状态
 * @param node  权限节点名称
 * @return 0 成功,负数失败
 */
int perm_grant(struct perm_state *state, const char *node);

/**
 * @brief 动态拒绝权限
 *
 * 添加一条 DENY 规则到覆盖列表.
 *
 * @param state 目标状态
 * @param node  权限节点名称
 * @return 0 成功,负数失败
 */
int perm_deny(struct perm_state *state, const char *node);

/**
 * @brief 切换 Profile
 *
 * @param state   目标状态
 * @param profile 新的 Profile
 */
void perm_state_attach_profile(struct perm_state *state, struct perm_profile *profile);

/**
 * @brief 检查位图(内部使用)
 *
 * 直接检查解析后的位图,不触发重新解析.
 *
 * @param state 目标状态
 * @param id    权限 ID
 * @return 是否拥有权限
 */
bool perm_check_bitmap(struct perm_state *state, perm_id_t id);

/* 解析函数 */

/**
 * @brief 解析权限状态
 *
 * 根据 Profile 和覆盖列表,重新计算权限位图.
 * 处理继承,通配符和优先级规则.
 *
 * @param state 目标状态
 */
void perm_resolve(struct perm_state *state);

/* 检查函数(热路径) */
struct process;

/**
 * @brief 检查进程权限
 *
 * 这是权限检查的主要入口.如果状态为 dirty,会自动触发解析.
 *
 * @param proc    目标进程
 * @param perm_id 权限 ID
 * @return true 如果拥有权限,false 否则
 */
bool perm_check(struct process *proc, perm_id_t perm_id);

/**
 * @brief 检查 I/O 端口权限
 *
 * @param proc 目标进程
 * @param port I/O 端口号
 * @return true 如果拥有权限,false 否则
 */
bool perm_check_ioport(struct process *proc, uint16_t port);

/**
 * @brief 按名称检查权限
 *
 * 较慢,建议优先使用 perm_check (ID).
 *
 * @param proc 目标进程
 * @param node 权限节点名称
 * @return true 如果拥有权限,false 否则
 */
bool perm_check_name(struct process *proc, const char *node);

/* Profile 函数 */

/**
 * @brief 初始化 Profile 系统
 */
void perm_profile_init(void);

/**
 * @brief 创建 Profile
 *
 * @param name Profile 名称
 * @return 新创建的 Profile,失败返回 NULL
 */
struct perm_profile *perm_profile_create(const char *name);

/**
 * @brief 设置 Profile 规则
 *
 * @param profile 目标 Profile
 * @param node    权限节点名称
 * @param value   权限值
 * @return 0 成功,负数失败
 */
int perm_profile_set(struct perm_profile *profile, const char *node, perm_value_t value);

/**
 * @brief 设置 Profile 继承
 *
 * @param child  子 Profile
 * @param parent 父 Profile
 * @return 0 成功,负数失败
 */
int perm_profile_inherit(struct perm_profile *child, struct perm_profile *parent);

/**
 * @brief 查找 Profile
 *
 * @param name Profile 名称
 * @return Profile 指针,未找到返回 NULL
 */
struct perm_profile *perm_profile_find(const char *name);

#endif /* XNIX_PERM_H */
