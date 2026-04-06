#ifndef _EXEC_H
#define _EXEC_H

/*
 * POSIX 风格 exec 接口边界说明。
 *
 * 当前系统已经具备：
 * - 按路径加载 ELF
 * - 创建新进程并传递 argv/handles
 *
 * 但当前还不具备“当前进程原位替换”为新映像的完整语义，因此：
 * - 这里暂不声明假的 execve/execv/execvp
 * - 普通用户态应使用 posix_spawn / posix_spawnp
 *
 * 当内核与运行时补齐：
 * - 当前线程/进程原位映像替换
 * - fd/handle 继承与关闭语义
 * - 与 wait/signal/job control 一致的行为
 * 后，再在这里提供真正的 exec* 家族接口。
 */

#endif /* _EXEC_H */
