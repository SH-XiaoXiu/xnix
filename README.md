# Xnix

在完成 [MyRTOS-Demo](https://github.com/SH-XiaoXiu/MyRTOS-Demo) (基于 ARM32 的 RTOS)后, 准备入手学习并实践现代操作系统的开发,引入现代操作系统的特性和设计.

以 x86 架构为示例开发, 但是系统内核最终将是平台无关的.

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
