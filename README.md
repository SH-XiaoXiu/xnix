# Xnix

Xnix 是一个 x86 32 位操作系统内核项目，旨在学习和实践现代操作系统开发。项目以 x86 架构为示例开发，但核心设计致力于实现平台无关性。

本项目在完成 [MyRTOS-Demo](https://github.com/SH-XiaoXiu/MyRTOS-Demo)（基于 ARM32 的 RTOS）后启动，旨在进一步探索微内核架构与更复杂的系统设计。

## 设计思路

### 微内核架构

Xnix 采用微内核设计，内核只保留最基本的功能（调度、IPC、内存管理、能力系统）

* **UDM (User Driver Model)**：驱动程序（如串口、键盘）运行在用户态，通过 IPC 与内核交互。
* **高可靠性**：驱动崩溃不会导致内核崩溃。

### 平台无关性

系统采用三层分离设计，确保内核核心逻辑不依赖具体硬件：

* **Arch (架构层)**：处理 CPU 架构强相关的逻辑（如 GDT/IDT、上下文切换）。
* **HAL (硬件抽象层)**：探测硬件特性，屏蔽底层差异。
* **Kernel (内核层)**：纯逻辑实现，不包含汇编或特定硬件指令。

### 抽象与封装

* **弱符号： `__attribute__((weak))` 提供默认实现，特定平台可按需覆盖，简化跨平台移植。
* **Opaque Types**：公共 API 使用不透明指针（就是句柄）（如 `thread_t`），隐藏内核内部数据结构细节，保证 API 稳定性。

## 工程结构

```text
main/
├── arch/x86/               # x86 架构实现（GDT, IDT, 启动代码）
├── include/                # 公共头文件
│   ├── arch/               # 架构抽象接口
│   ├── drivers/            # 内核态驱动接口
│   └── xnix/               # 系统公共 API
├── kernel/                 # 内核子系统（平台无关）
│   ├── ipc/                # IPC 机制
│   ├── cap/                # Capability 能力系统
│   ├── mm/                 # 内存管理
│   ├── process/            # 进程管理
│   └── sched/              # 调度器
├── lib/                    # 内核通用库（同步原语等）
└── drivers/                # 内核态控制台框架（早期输出）

user/                       # 用户态代码
├── drivers/                # UDM 驱动（seriald, kbd 等）
├── init/                   # init 进程
└── lib/                    # 用户态库（Syscall 封装）
```

## 系统架构

### 整体架构图

```mermaid
graph TD
    subgraph UserSpace [用户空间]
        direction TB
        Init[Init进程]
        SerialD[Serial驱动_UDM]
        KbdD[Keyboard驱动_UDM]
        UserLib[用户库]
    end

    subgraph KernelSpace [内核空间]
        direction TB
        
        subgraph Kernel [内核层]
            Sched[调度器]
            IPC[IPC通信]
            MM[内存管理]
            Cap[能力系统]
        end

        subgraph HAL [硬件抽象层]
            Feature[特性探测]
            Probe[硬件扫描]
        end

        subgraph Arch [架构层]
            GDT_IDT[GDT_IDT]
            Int[中断处理]
            Boot[启动代码]
        end
    end

    Init <-->|系统调用| Kernel
    SerialD <-->|IPC| Kernel
    KbdD <-->|IPC| Kernel
    
    Kernel --> HAL
    HAL --> Arch
    
    style UserSpace fill:#e1f5fe,stroke:#01579b
    style KernelSpace fill:#fff3e0,stroke:#ff6f00
```

### 启动流程图

```mermaid
graph TD
    Start((上电启动)) --> Bootloader[GRUB_Multiboot]
    Bootloader --> ArchEarly[架构早期初始化]
    ArchEarly --> Console[控制台初始化]
    Console --> Core[核心初始化]
    Core --> Subsys[子系统初始化]
    Subsys --> Late[后期初始化]
    Late --> Services[启动服务]
    Services --> UserMode((进入用户态))
    
    UserMode --> InitProcess[Init进程运行]
    InitProcess --> LoadDrivers[加载UDM驱动]
```

### 架构设计图

```mermaid
%%{init: {'flowchart': {'nodeSpacing': 20, 'rankSpacing': 40}, 'themeVariables': {'fontSize': '16px'}}}%%
graph TB
    subgraph UserSpace["用户态 user/"]
        direction TB
        U_Init["Init进程"]
        U_DrvSerial["seriald UDM驱动"]
        U_DrvKbd["kbd UDM驱动"]
        U_App["用户程序/服务"]
        U_Libc["user/libc: syscall wrapper"]
    end

    subgraph KernelSpace["内核态 main/kernel/"]
        direction TB
        K_Sys["syscall分发: kernel/sys/syscall.c"]
        K_Cap["Capability: kernel/capability/"]
        K_IPC["IPC: endpoint/notification: kernel/ipc/"]
        K_Sched["调度: kernel/sched/"]
        K_Proc["进程/加载: kernel/process/"]
        K_MM["内存管理: kernel/mm/"]
    end

    subgraph ArchHAL["架构与HAL main/arch/x86/"]
        direction TB
        A_IRQ["中断/定时器: pit.c"]
        A_Ctx["上下文切换: context.s"]
        A_AS["地址空间切换: process.c"]
    end

    HW["硬件"]

    U_Init --> U_Libc
    U_DrvSerial --> U_Libc
    U_DrvKbd --> U_Libc
    U_App --> U_Libc

    U_Libc --> K_Sys

    K_Sys --> K_Cap
    K_Sys --> K_IPC
    K_Sys --> K_Proc
    K_Sys --> K_MM

    K_IPC --> K_Sched
    K_Sched --> A_Ctx
    K_Proc --> A_AS
    A_IRQ --> K_Sched
    A_AS --> HW
    A_IRQ --> HW
```

### 调度图

```mermaid
%%{init: {'flowchart': {'nodeSpacing': 20, 'rankSpacing': 40}, 'themeVariables': {'fontSize': '16px'}}}%%
graph TD
    IRQ0["IRQ0 PIT"] --> TimerTick["timer_tick()"]
    TimerTick --> SchedTick["sched_tick()"]
    SchedTick --> WakeSleep["sleep_check_wakeup()\n扫描 blocked_list"]
    SchedTick --> NeedResched{"policy.tick()\n需要抢占?"}
    NeedResched -->|否| ContinueRun["继续运行 current"]
    NeedResched -->|是| Schedule["schedule()"]

    subgraph Queues["关键队列/状态"]
        RunQ["runqueue\npolicy_rr FIFO"]
        Blocked["blocked_list\nwait_chan"]
        Zombie["zombie_list"]
    end

    Schedule --> Pick["policy.pick_next()"]
    Pick --> Switch["context_switch()\ncontext_switch_first()"]
    Switch --> Cleanup["sched_cleanup_zombies()"]

    IPCSendPath["ipc_send/ipc_call\n无receiver?"] -->|sender入队并阻塞| BlockSend["sched_block\n(wait_chan=endpoint)"]
    IPCRecvPath["ipc_receive\n无sender?"] -->|receiver入队并阻塞| BlockRecv["sched_block\n(wait_chan=endpoint)"]

    BlockSend --> Blocked
    BlockRecv --> Blocked
    WakeSleep -->|wakeup_tick到期| RunQ
    Pick --> RunQ
    Cleanup --> Zombie
```

### IPC图（Endpoint/RPC/异步/Notification + Capability）

```mermaid
%%{init: {'flowchart': {'nodeSpacing': 20, 'rankSpacing': 40}, 'themeVariables': {'fontSize': '16px'}}}%%
graph TB
    subgraph User["用户态"]
        U_S["发送方线程"]
        U_R["接收方线程"]
    end

    subgraph Syscall["系统调用边界 kernel/sys/syscall.c"]
        CopyIn["syscall_ipc_msg_copy_in()"]
        CopyOut["syscall_ipc_msg_copy_out()"]
    end

    subgraph Cap["能力系统 kernel/capability/"]
        LookupW["cap_lookup: CAP_WRITE"]
        LookupR["cap_lookup: CAP_READ"]
    end

    subgraph Endpoint["Endpoint对象 kernel/ipc/endpoint.c"]
        SQ["send_queue"]
        RQ["recv_queue"]
        AQ["async_queue"]
        CopyMsg["ipc_copy_msg()"]
        Peer["thread.ipc_peer"]
    end

    subgraph Sched["调度器 kernel/sched/"]
        Block["sched_block(wait_chan)"]
        WakeT["sched_wakeup_thread()"]
        FindBlocked["sched_lookup_blocked()"]
    end

    subgraph Notif["Notification(已实现, syscall暂未导出)"]
        N_Sig["notification_signal(bits)"]
        N_Wait["notification_wait()"]
    end

    U_S -->|SYS_IPC_SEND,SYS_IPC_CALL| CopyIn
    U_R -->|SYS_IPC_RECV| CopyIn
    U_R -->|SYS_IPC_REPLY| CopyIn
    U_S -.->|ipc_send_async| CopyIn

    CopyIn --> LookupW --> SQ
    CopyIn --> LookupR --> RQ
    CopyIn --> LookupW --> AQ
    CopyIn --> FindBlocked

    SQ -->|"有receiver在等"| CopyMsg --> WakeT --> CopyOut --> U_R
    SQ -->|"无receiver"| Block

    RQ -->|"有sender在等"| CopyMsg --> Peer --> CopyOut --> U_R
    RQ -->|"无sender"| Block

    FindBlocked --> CopyMsg --> WakeT --> U_S

    N_Wait --> Block
    N_Sig --> WakeT
```

## 如何运行

### 环境依赖
```bash
# 安装构建工具和 QEMU
sudo apt install gcc gcc-multilib grub-pc-bin xorriso qemu-system-x86 make cmake
```

### 快速开始

`run` 脚本

```bash

./run -f                 # 清理 + 并行编译 + 运行

./run                    # 增量编译并运行

# 查看所有选项
./run --help
```

### 常用示例

```bash
./run -f                 # 构建运行
./run                    # 增量编译运行
./run -f -i              # 构建 + ISO 完整测试
./run -d                 # 调试模式（等待 GDB :1234）

# 清理操作
./run --clean            # 只清理不编译
./run --clean-all        # 完全删除 build
./run --rebuild          # 完全重建（删除 build + 重新配置）

# QEMU 硬件配置
./run -f --mem 256M --smp 2    # 指定内存和 CPU 核心
./run -f --mem 512M --smp 4    # 更大配置

# CMake 编译配置
./run -DENABLE_SMP=ON -DCFG_MAX_CPUS=8     # 启用 SMP
./run -DCFG_DEBUG_SCHED=ON                 # 调度器调试
./run -DENABLE_VMM_DEBUG=ON                # VMM 调试

# 额外功能
./run -c                       # 只编译不运行
./run -i                       # 使用 ISO 启动
./run --qemu "-serial file:serial.log"    # 串口输出到文件
```

### 调试流程

```bash
# 终端 1: 启动调试模式
./run -d

# 终端 2: 连接 GDB
gdb build/xnix.elf -ex "target remote :1234"
```

### Make 方式

```bash
# 初始化
mkdir build && cd build
cmake ..

# 并行编译运行
make -j$(nproc) iso
make run

# 调试
make debug
```

## 用户代码

所有用户态代码位于 `user/` 目录下：

* **驱动开发**：在 `user/drivers/` 下创建新的驱动。建议使用 UDM 模式，驱动即进程，通过 IPC 提供服务。
* **应用程序**：在 `user/init/` 或新建目录开发应用。
* **系统调用**：用户态通过 `user/lib/` 提供的库函数发起系统调用。

## 资料来源

* 《Linux内核设计与实现》(Linux Kernel Development)
* https://ysos.gzti.me/
* https://www.intel.com/sdm
* https://wiki.osdev.org/Spinlock
* https://www.l4hq.org/
* https://www.bilibili.com/video/BV1TTUfBYEEA/

## 其他

* **作者**: XiaoXiu
* **License**: MIT License
