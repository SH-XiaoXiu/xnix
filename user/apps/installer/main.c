/**
 * @file installer/main.c
 * @brief Xnix 系统安装程序
 *
 * 将预制的磁盘镜像写入到物理磁盘
 *
 * 用法: installer <目标磁盘号>
 * 示例: installer 0    # 安装到主盘 (hda)
 *       installer 1    # 安装到从盘 (hdb)
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vfs_client.h>
#include <xnix/ipc/fs.h>
#include <xnix/syscall.h>

#define ATA_SECTOR_SIZE 512
#define BUFFER_SECTORS  64 /* 每次传输 32KB */

/* ATA I/O 端口 */
#define ATA_DATA         0x1F0
#define ATA_FEATURES     0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW      0x1F3
#define ATA_LBA_MID      0x1F4
#define ATA_LBA_HIGH     0x1F5
#define ATA_DRIVE_HEAD   0x1F6
#define ATA_STATUS       0x1F7
#define ATA_COMMAND      0x1F7

#define ATA_CTRL_COMMAND 0x3F6

/* ATA 命令 */
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY    0xEC

/* ATA 状态位 */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

static uint8_t g_buffer[BUFFER_SECTORS * ATA_SECTOR_SIZE] __attribute__((aligned(4)));

static void ata_wait_bsy(void) {
    while (sys_ioport_inb(ATA_STATUS) & ATA_SR_BSY)
        ;
}

static void ata_wait_drq(void) {
    while (!(sys_ioport_inb(ATA_STATUS) & ATA_SR_DRQ))
        ;
}

/**
 * 检查磁盘是否就绪
 */
static bool ata_is_ready(uint8_t drive) {
    sys_ioport_outb(ATA_DRIVE_HEAD, (drive == 0 ? 0xA0 : 0xB0));
    sys_ioport_inb(ATA_STATUS);
    sys_ioport_inb(ATA_STATUS);
    sys_ioport_inb(ATA_STATUS);
    sys_ioport_inb(ATA_STATUS);
    return (sys_ioport_inb(ATA_STATUS) & ATA_SR_DRDY) != 0;
}

/**
 * 获取磁盘扇区数
 */
static uint32_t ata_get_sector_count(uint8_t drive) {
    uint16_t buf[256];

    ata_wait_bsy();
    sys_ioport_outb(ATA_DRIVE_HEAD, (drive == 0 ? 0xA0 : 0xB0));
    sys_ioport_outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    if (!(sys_ioport_inb(ATA_STATUS) & ATA_SR_DRQ)) {
        return 0;
    }

    ata_wait_drq();
    for (int i = 0; i < 256; i++) {
        buf[i] = sys_ioport_inw(ATA_DATA);
    }

    /* 字 60-61 是 28-bit LBA 扇区数 */
    return (uint32_t)buf[60] | ((uint32_t)buf[61] << 16);
}

/**
 * 写入扇区
 */
static int ata_write(uint8_t drive, uint32_t lba, uint32_t count, const void *buffer) {
    const uint16_t *buf = (const uint16_t *)buffer;

    ata_wait_bsy();
    sys_ioport_outb(ATA_DRIVE_HEAD, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    sys_ioport_outb(ATA_SECTOR_COUNT, (uint8_t)count);
    sys_ioport_outb(ATA_LBA_LOW, (uint8_t)lba);
    sys_ioport_outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    sys_ioport_outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    sys_ioport_outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

    for (uint32_t i = 0; i < count; i++) {
        ata_wait_bsy();
        ata_wait_drq();

        if (sys_ioport_inb(ATA_STATUS) & ATA_SR_ERR) {
            return -1;
        }

        for (int j = 0; j < 256; j++) {
            sys_ioport_outw(ATA_DATA, buf[i * 256 + j]);
        }
    }

    ata_wait_bsy();
    sys_ioport_outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy();

    return 0;
}

/**
 * 打印进度条
 */
static void print_progress(uint32_t current, uint32_t total) {
    int percent   = (int)((current * 100) / total);
    int bar_width = 50;
    int filled    = (percent * bar_width) / 100;

    printf("\r[");
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) {
            printf("#");
        } else {
            printf(" ");
        }
    }
    printf("] %d%% (%u/%u MB)", percent, current / 2048, total / 2048);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("用法: installer <目标磁盘号>\n");
        printf("示例:\n");
        printf("  installer 0    # 安装到主盘 (hda)\n");
        printf("  installer 1    # 安装到从盘 (hdb)\n");
        return 1;
    }

    /* 简单解析数字 */
    uint8_t target_drive = 0;
    if (argv[1][0] >= '0' && argv[1][0] <= '9') {
        target_drive = (uint8_t)(argv[1][0] - '0');
    }
    if (target_drive > 1) {
        printf("错误: 无效的磁盘号 %d (仅支持 0 或 1)\n", target_drive);
        return 1;
    }

    printf("\n");
    printf("========================================\n");
    printf("  Xnix 操作系统安装程序\n");
    printf("========================================\n");
    printf("\n");

    /* 初始化 ATA */
    sys_ioport_outb(ATA_CTRL_COMMAND, 0x02);

    /* 检查目标磁盘 */
    printf("检测目标磁盘 hd%c...\n", 'a' + target_drive);
    if (!ata_is_ready(target_drive)) {
        printf("错误: 磁盘 hd%c 未就绪\n", 'a' + target_drive);
        return 1;
    }

    uint32_t disk_sectors = ata_get_sector_count(target_drive);
    if (disk_sectors == 0) {
        printf("错误: 无法识别磁盘 hd%c\n", 'a' + target_drive);
        return 1;
    }

    uint32_t disk_mb = disk_sectors / 2048;
    printf("磁盘容量: %u MB (%u 扇区)\n", disk_mb, disk_sectors);

    /* 打开镜像文件 */
    const char *image_path = "/sys/disk_template.img";
    printf("\n打开系统镜像: %s\n", image_path);

    int fd = vfs_open(image_path, FS_O_RDONLY);
    if (fd < 0) {
        printf("错误: 无法打开镜像文件 (返回值 %d)\n", fd);
        printf("提示: 请确保从 Xnix ISO 启动\n");
        return 1;
    }

    /* 获取镜像大小 */
    struct vfs_stat st;
    vfs_close(fd);
    if (vfs_stat(image_path, &st) < 0) {
        printf("错误: 无法获取镜像大小\n");
        return 1;
    }

    /* 重新打开文件 */
    fd = vfs_open(image_path, FS_O_RDONLY);
    if (fd < 0) {
        printf("错误: 无法重新打开镜像文件\n");
        return 1;
    }

    uint32_t image_sectors = (st.size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
    uint32_t image_mb      = (st.size + 1024 * 1024 - 1) / (1024 * 1024);

    printf("镜像大小: %u MB (%u 扇区)\n", image_mb, image_sectors);

    if (image_sectors > disk_sectors) {
        printf("错误: 目标磁盘空间不足 (需要 %u 扇区, 仅有 %u 扇区)\n", image_sectors,
               disk_sectors);
        vfs_close(fd);
        return 1;
    }

    /* 确认安装 */
    printf("\n警告: 这将清除磁盘 hd%c 上的所有数据!\n", 'a' + target_drive);
    printf("是否继续? (输入 YES 继续): ");
    fflush(stdout);

    char confirm[16];
    int i = 0;
    int c;
    while (i < (int)sizeof(confirm) - 1) {
        c = getchar();
        if (c == EOF || c == '\n') {
            break;
        }
        confirm[i++] = (char)c;
    }
    confirm[i] = '\0';

    if (strcmp(confirm, "YES") != 0) {
        printf("\n安装已取消\n");
        vfs_close(fd);
        return 1;
    }

    /* 开始安装 */
    printf("\n开始安装系统...\n");

    uint32_t current_lba = 0;
    ssize_t  nread;

    while ((nread = vfs_read(fd, g_buffer, sizeof(g_buffer))) > 0) {
        uint32_t sectors_to_write = (nread + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;

        /* 不足一个扇区时补零 */
        if (nread % ATA_SECTOR_SIZE != 0) {
            memset(g_buffer + nread, 0, sectors_to_write * ATA_SECTOR_SIZE - nread);
        }

        if (ata_write(target_drive, current_lba, sectors_to_write, g_buffer) < 0) {
            printf("\n错误: 写入扇区 %u 失败\n", current_lba);
            vfs_close(fd);
            return 1;
        }

        current_lba += sectors_to_write;
        print_progress(current_lba, image_sectors);
    }

    vfs_close(fd);

    printf("\n\n安装完成!\n");
    printf("\n");
    printf("========================================\n");
    printf("  后续步骤:\n");
    printf("========================================\n");
    printf("1. 拔出 USB 启动盘\n");
    printf("2. 重启计算机\n");
    printf("3. 在 BIOS 中设置从 hd%c 启动\n", 'a' + target_drive);
    printf("4. 享受 Xnix!\n");
    printf("\n");

    return 0;
}
