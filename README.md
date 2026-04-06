# Xnix

一个用于学习和实验微内核设计的 32 位 x86 操作系统。

![系统启动界面](docs/assets/screenshot_24ff5336.png)

## 当前状态

- `init` 自举
- 用户态驱动与系统服务启动
- `vfs` + `fatfs` 根文件系统
- `devfs`
- `term` + `console`
- 多串口 shell
- VGA 多虚拟终端
- GUI session 与 `ws`
- `/dev/ttyN`
- 进程退出后 GUI 窗口自动回收

## 设计原则

Xnix 明确按微内核方式分层：

- 内核只提供机制
  - 调度
  - IPC
  - 内存管理
  - 进程/线程生命周期
  - IRQ
  - handle / capability
- 策略在用户态
  - 服务编排
  - VFS
  - 终端
  - console/session 管理
  - 图形系统
  - 驱动

当前已经完成的一些关键收口：

- IPC `send` / `call` 语义分离
- VFS 路径层与对象层分离
- 普通目录直连后端对象
- TTY 主路径统一到 `IO_READ / IO_WRITE / IO_CLOSE / IO_IOCTL`
- `console` 独立承担前台 session 管理
- `BOOT -> TTY -> GUI` 会话模型已建立

## 运行时目录

当前运行时目录已经收敛为：

- `/bin`：普通应用与工具
- `/sbin`：系统服务
- `/sbin/driver`：驱动
- `/etc`：配置
- `/dev`：设备节点
- `/boot`：启动资源

程序角色靠运行时目录区分，不再靠名字后缀 `d/s`。

## 当前核心组件

### 服务

- `/sbin/vfs.elf`
- `/sbin/devfs.elf`
- `/sbin/term.elf`
- `/sbin/console.elf`
- `/sbin/syslog.elf`
- `/sbin/klog.elf`
- `/sbin/ws.elf`

### 驱动

- `/sbin/driver/serial.elf`
- `/sbin/driver/ps2.elf`
- `/sbin/driver/display.elf`
- `/sbin/driver/fatfs.elf`
- `/sbin/driver/fb.elf`

### 常用应用

- `/bin/shell.elf`
- `/bin/ls.elf`
- `/bin/cat.elf`
- `/bin/write.elf`
- `/bin/chvt.elf`
- `/bin/ttysend.elf`
- `/bin/test_gui.elf`
- `/bin/sudo.elf`
- `/bin/sudod.elf`

## 已验证能力

当前已经验证过的典型场景：

- `ls /`、`ls /bin`、`cat`、`write`
- 当前工作目录与路径解析
- 多串口终端并行工作
- 共享 IRQ 串口分发
- `/dev/tty0..tty6`
- `ttysend /dev/ttyN "cmd\n"`
- VGA 多 VT：`chvt 0/5/6`
- `chvt gui`
- `test_gui`
- GUI 进程退出后窗口自动回收
- `run ls`、`run test_args a b c`、`run test_gui`

## 用户态库

当前用户态保留库集合：

- `libsys`
- `libc`
- `libpthread`
- `libposix`
- `libdriver`
- `libws`
- `libfont`
- `libblock`
- `libproc`（暂留）


### 库职责

#### `libsys`
- 原始 syscall / ABI / IPC / server helper

#### `libc`
- C 运行时
- `stdio` / `malloc` / `string` / `errno`
- fd / VFS / IO 基础封装
- 当前仍直接实现：
  - `dup`
  - `dup2`
  - `pipe`

#### `libposix`
- 高层 POSIX 风格接口
- 当前已落地：
  - `posix_spawn`
  - `posix_spawnp`
  - `waitpid`
  - `wait`

## 微内核语义说明

### 关于 `fork`

Xnix 未来会支持 POSIX 风格的 `fork`，但不应是一个大而全的 `SYS_FORK`。

### 关于 `exec`

当前系统具备的是：

- 按路径加载 ELF
- 创建新进程并传递 argv/handles

当前**还不具备**：

- 当前进程原位映像替换

## 构建与运行

### 依赖

Ubuntu / Debian:

```bash
sudo apt install gcc gcc-multilib grub-pc-bin xorriso qemu-system-x86 make cmake
```

### 构建并运行

```bash
git clone https://github.com/sh-xiaoxiu/xnix.git
cd xnix
./run -b -i
```

常用命令：

```bash
./run
./run -b
./run -i
./run -b -i
./run -d -i
./run --install src.img --hda dst.img
```

### 多串口验证

```bash
./run -i -b \
  --serial telnet:4321 \
  --extra-serial telnet:4322 \
  --extra-serial telnet:4323 \
  --extra-serial telnet:4324
```

对应关系：

- `COM1 -> tty1`
- `COM2 -> tty2`
- `COM3 -> tty3`
- `COM4 -> tty4`

## 启动与会话模型

当前系统启动路径大致是：

1. `init` 启动内置 `ramfsd`
2. 解包 `initramfs`
3. 读取 `/etc/sys.conf`
4. 启动核心驱动与服务
5. 迁移到 `vfs`
6. `fatfs` 接管根目录
7. `term` 建立 `tty0..tty6`
8. `console` 建立前台 session
9. `BOOT -> tty0` handoff
10. 启动 shell / ws / 其他服务

当前前台会话模型：

- `BOOT`
- `TTY`
- `GUI`

由 `console` 统一管理。

## 项目结构

```text
xnix/
├── main/                    # 内核
├── sdk/                     # 公共 ABI / 协议头
├── user/
│   ├── init/                # init、自举、服务管理
│   ├── apps/                # 普通应用
│   ├── drivers/             # 用户态驱动源码
│   ├── servers/             # 系统服务源码
│   └── libs/                # 用户态库
├── iso/                     # 镜像打包
└── run                      # 构建/运行脚本
```

## 当前待继续推进的方向

1. `libposix` 继续扩展
2. `libproc` 继续收缩
3. `fork` 所需的底层机制设计
4. 真正的 `exec*` 支持
5. 更完整的 `PATH` / 环境变量语义
6. `termios` / `poll` / `fcntl`

