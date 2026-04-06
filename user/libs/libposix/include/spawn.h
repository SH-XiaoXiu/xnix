#ifndef XNIX_POSIX_SPAWN_H
#define XNIX_POSIX_SPAWN_H

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

#endif /* XNIX_POSIX_SPAWN_H */
