#ifndef XNIX_POSIX_SPAWN_H
#define XNIX_POSIX_SPAWN_H

#include <xnix/abi/process.h>

/*
 * POSIX 风格进程启动骨架。
 *
 * 这是普通用户态代码应优先使用的高层进程启动接口。
 * 只有在需要显式构造 handle/stdio/image 参数图时，才应直接回退到 libproc。
 *
 * 当前约束：
 * - `posix_spawn`：调用方提供已解析好的路径
 * - `posix_spawnp`：按当前系统默认搜索路径执行 PATH 查找
 *   (`/bin`、`/sbin`、`/mnt/bin`)
 * - 当前还没有完整的环境变量 PATH 语义
 * - 当前签名仍是简化版，不是完整 POSIX `posix_spawn(3)` 参数集合
 */

int posix_spawn(const char *path, int argc, const char **argv);
int posix_spawnp(const char *file, int argc, const char **argv);

/*
 * 构造一个标准“继承 named handles + argv”的 exec 请求。
 *
 * 这是给像 sudo 这类需要把 exec 请求转交给别的服务的场景准备的高层包装，
 * 避免普通应用直接依赖 proc builder。
 */
int posix_spawn_make_exec_args(struct abi_exec_args *out, const char *path,
                               int argc, const char **argv);

#endif /* XNIX_POSIX_SPAWN_H */
