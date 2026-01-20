# Xnix

在完成 [MyRTOS-Demo](https://github.com/SH-XiaoXiu/MyRTOS-Demo) (基于 ARM32 的 RTOS)后, 准备入手学习并实践现代操作系统的开发,引入现代操作系统的特性和设计.

以 x86 架构为示例开发, 但是系统内核最终将是平台无关的.

## 工程结构

```
kernel/
├── arch/           # 架构层
│   ├── *.h         # CMake 生成的包装头文件
│   └── <arch>/     # 具体架构实现（如 x86）
├── drivers/        # 驱动接口（平台无关的头文件）
├── xstd/           # 内核标准库（头文件）
└── main/           # 内核主体代码（平台无关）
```

### 设计思路

**平台无关**：内核主体代码通过 `<arch/*.h>` 抽象层访问硬件，不直接依赖具体架构。

**填空式移植**：新架构只需实现少量接口函数，由弱符号机制自动覆盖默认实现：

```c
// kernel/main/xstd/stdout.c 中的弱符号默认实现
__attribute__((weak)) void arch_putc(char c) { (void)c; }

// kernel/arch/x86/drivers/console.c 中的强符号覆盖
void arch_putc(char c) {
    serial_putc(SERIAL_COM1, c);
    vga_putc(c);
}
```

### 头文件引用规范

| 前缀 | 用途 | 示例 |
|------|------|------|
| `<arch/...>` | 架构抽象接口 | `<arch/console.h>` |
| `<drivers/...>` | 驱动接口 | `<drivers/vga.h>` |
| `<xstd/...>` | 内核标准库 | `<xstd/stdout.h>` |

## 构建 & 运行

**依赖** (Ubuntu/WSL):
```bash
sudo apt install gcc gcc-multilib grub-pc-bin xorriso qemu-system-x86
```

**运行**:
```bash
./scripts/run.sh
```

**调试** (GDB):
```bash
# 终端1
./scripts/debug.sh

# 终端2
gdb build/xnix.elf -ex "target remote localhost:1234"
```
