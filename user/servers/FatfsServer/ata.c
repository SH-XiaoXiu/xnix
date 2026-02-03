/**
 * @file ata.c
 * @brief ATA PIO 驱动实现
 *
 * 实现 ATA PIO 模式的磁盘读写,用于 QEMU 虚拟 IDE 硬盘.
 * 参考: OSDev ATA PIO Mode
 */

#include "ata.h"

#include <stdio.h>
#include <xnix/syscall.h>

/* ATA 主通道端口 */
#define ATA_PRIMARY_IO   0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

/* ATA 寄存器偏移(相对于 IO 基址) */
#define ATA_REG_DATA     0
#define ATA_REG_ERROR    1
#define ATA_REG_FEATURES 1
#define ATA_REG_SECCOUNT 2
#define ATA_REG_LBA_LO   3
#define ATA_REG_LBA_MID  4
#define ATA_REG_LBA_HI   5
#define ATA_REG_DRIVE    6
#define ATA_REG_STATUS   7
#define ATA_REG_COMMAND  7

/* 控制寄存器偏移(相对于 CTRL 基址) */
#define ATA_REG_ALTSTATUS 0
#define ATA_REG_DEVCTRL   0

/* ATA 命令 */
#define ATA_CMD_READ_PIO  0x20
#define ATA_CMD_WRITE_PIO 0x30
#define ATA_CMD_IDENTIFY  0xEC
#define ATA_CMD_FLUSH     0xE7

/* ATA 状态位 */
#define ATA_SR_BSY  0x80 /* 忙 */
#define ATA_SR_DRDY 0x40 /* 驱动器就绪 */
#define ATA_SR_DRQ  0x08 /* 数据请求 */
#define ATA_SR_ERR  0x01 /* 错误 */

/* 驱动器选择 */
#define ATA_DRIVE_MASTER 0xE0
#define ATA_DRIVE_SLAVE  0xF0

static uint32_t g_io_cap;          /* 数据端口 capability (0x1F0-0x1F7) */
static uint32_t g_ctrl_cap;        /* 控制端口 capability (0x3F6-0x3F7) */
static uint32_t g_sector_count[2]; /* 主盘/从盘扇区数 */
static bool     g_disk_present[2];

/* 根据端口选择正确的 capability */
static inline uint32_t get_cap_for_port(uint16_t port) {
    return (port >= 0x3F6) ? g_ctrl_cap : g_io_cap;
}

/* I/O 操作封装 */
static inline void ata_outb(uint16_t port, uint8_t val) {
    sys_ioport_outb(get_cap_for_port(port), port, val);
}

static inline uint8_t ata_inb(uint16_t port) {
    int ret = sys_ioport_inb(get_cap_for_port(port), port);
    return (ret >= 0) ? (uint8_t)ret : 0xFF;
}

static inline void ata_outw(uint16_t port, uint16_t val) {
    sys_ioport_outw(get_cap_for_port(port), port, val);
}

static inline uint16_t ata_inw(uint16_t port) {
    int ret = sys_ioport_inw(get_cap_for_port(port), port);
    return (ret >= 0) ? (uint16_t)ret : 0xFFFF;
}

/* 400ns 延迟(读取 4 次备用状态寄存器) */
static void ata_delay(void) {
    for (int i = 0; i < 4; i++) {
        ata_inb(ATA_PRIMARY_CTRL + ATA_REG_ALTSTATUS);
    }
}

/* 等待 BSY 清除 */
static int ata_wait_bsy(void) {
    int timeout = 100000;
    while (timeout--) {
        uint8_t status = ata_inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1;
}

/* 等待 DRQ 置位 */
static int ata_wait_drq(void) {
    int timeout = 100000;
    while (timeout--) {
        uint8_t status = ata_inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return -1;
        }
        if (status & ATA_SR_DRQ) {
            return 0;
        }
    }
    return -1;
}

/* 选择驱动器 */
static void ata_select_drive(uint8_t drive) {
    uint8_t drv = (drive == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    ata_outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, drv);
    ata_delay();
}

/* 检测磁盘 */
static bool ata_identify(uint8_t drive, uint16_t *info) {
    ata_select_drive(drive);

    /* 清零扇区计数和 LBA 寄存器 */
    ata_outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 0);
    ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, 0);
    ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, 0);
    ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, 0);

    /* 发送 IDENTIFY 命令 */
    ata_outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    /* 检查状态 */
    uint8_t status = ata_inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (status == 0) {
        return false; /* 磁盘不存在 */
    }

    /* 等待 BSY 清除 */
    if (ata_wait_bsy() < 0) {
        return false;
    }

    /* 检查 LBA_MID 和 LBA_HI(非 ATA 设备会设置这些寄存器) */
    if (ata_inb(ATA_PRIMARY_IO + ATA_REG_LBA_MID) != 0 ||
        ata_inb(ATA_PRIMARY_IO + ATA_REG_LBA_HI) != 0) {
        return false; /* 不是 ATA 设备 */
    }

    /* 等待 DRQ 或 ERR */
    if (ata_wait_drq() < 0) {
        return false;
    }

    /* 读取 256 个 16 位字 */
    for (int i = 0; i < 256; i++) {
        info[i] = ata_inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    }

    return true;
}

int ata_init(uint32_t io_cap, uint32_t ctrl_cap) {
    g_io_cap          = io_cap;
    g_ctrl_cap        = ctrl_cap;
    g_disk_present[0] = false;
    g_disk_present[1] = false;
    g_sector_count[0] = 0;
    g_sector_count[1] = 0;

    uint16_t info[256];

    /* 检测主盘 */
    if (ata_identify(0, info)) {
        g_disk_present[0] = true;
        /* 扇区数在 word 60-61 (LBA28) */
        g_sector_count[0] = (uint32_t)info[60] | ((uint32_t)info[61] << 16);
    }

    /* 检测从盘 */
    if (ata_identify(1, info)) {
        g_disk_present[1] = true;
        g_sector_count[1] = (uint32_t)info[60] | ((uint32_t)info[61] << 16);
    }

    if (!g_disk_present[0] && !g_disk_present[1]) {
        return -1;
    }

    return 0;
}

bool ata_is_ready(uint8_t drive) {
    if (drive > 1) {
        return false;
    }
    return g_disk_present[drive];
}

int ata_read(uint8_t drive, uint32_t lba, uint32_t count, void *buffer) {
    if (drive > 1 || !g_disk_present[drive]) {
        return -1;
    }
    if (count == 0 || count > 256) {
        return -1;
    }

    uint8_t *buf = (uint8_t *)buffer;
    uint8_t  drv = (drive == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t sector = lba + i;

        /* 等待驱动器就绪 */
        if (ata_wait_bsy() < 0) {
            return -1;
        }

        /* 设置 LBA 和扇区数 */
        ata_outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, drv | 0x40 | ((sector >> 24) & 0x0F));
        ata_outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 1);
        ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, sector & 0xFF);
        ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (sector >> 8) & 0xFF);
        ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, (sector >> 16) & 0xFF);

        /* 发送读命令 */
        ata_outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

        /* 等待数据就绪 */
        if (ata_wait_drq() < 0) {
            return -1;
        }

        /* 读取 256 个 16 位字 */
        uint16_t *ptr = (uint16_t *)(buf + i * ATA_SECTOR_SIZE);
        for (int j = 0; j < 256; j++) {
            ptr[j] = ata_inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        }
    }

    return 0;
}

int ata_write(uint8_t drive, uint32_t lba, uint32_t count, const void *buffer) {
    if (drive > 1 || !g_disk_present[drive]) {
        return -1;
    }
    if (count == 0 || count > 256) {
        return -1;
    }

    const uint8_t *buf = (const uint8_t *)buffer;
    uint8_t        drv = (drive == 0) ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t sector = lba + i;

        /* 等待驱动器就绪 */
        if (ata_wait_bsy() < 0) {
            return -1;
        }

        /* 设置 LBA 和扇区数 */
        ata_outb(ATA_PRIMARY_IO + ATA_REG_DRIVE, drv | 0x40 | ((sector >> 24) & 0x0F));
        ata_outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 1);
        ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_LO, sector & 0xFF);
        ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (sector >> 8) & 0xFF);
        ata_outb(ATA_PRIMARY_IO + ATA_REG_LBA_HI, (sector >> 16) & 0xFF);

        /* 发送写命令 */
        ata_outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

        /* 等待 DRQ */
        if (ata_wait_drq() < 0) {
            return -1;
        }

        /* 写入 256 个 16 位字 */
        const uint16_t *ptr = (const uint16_t *)(buf + i * ATA_SECTOR_SIZE);
        for (int j = 0; j < 256; j++) {
            ata_outw(ATA_PRIMARY_IO + ATA_REG_DATA, ptr[j]);
        }

        /* 刷新缓存 */
        ata_outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_FLUSH);
        if (ata_wait_bsy() < 0) {
            return -1;
        }
    }

    return 0;
}

uint32_t ata_get_sector_count(uint8_t drive) {
    if (drive > 1) {
        return 0;
    }
    return g_sector_count[drive];
}
