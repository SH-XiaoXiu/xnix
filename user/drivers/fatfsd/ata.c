#include "ata.h"

#include <stdio.h>
#include <string.h>
#include <xnix/syscall.h>

/* I/O 端口定义 */
#define ATA_DATA         0x1F0
#define ATA_FEATURES     0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW      0x1F3
#define ATA_LBA_MID      0x1F4
#define ATA_LBA_HIGH     0x1F5
#define ATA_DRIVE_HEAD   0x1F6
#define ATA_STATUS       0x1F7
#define ATA_COMMAND      0x1F7

#define ATA_CTRL_STATUS  0x3F6
#define ATA_CTRL_COMMAND 0x3F6

/* 命令 */
#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY    0xEC

/* 状态位 */
#define ATA_SR_BSY  0x80 /* Busy */
#define ATA_SR_DRDY 0x40 /* Drive ready */
#define ATA_SR_DF   0x20 /* Drive write fault */
#define ATA_SR_DSC  0x10 /* Drive seek complete */
#define ATA_SR_DRQ  0x08 /* Data request ready */
#define ATA_SR_CORR 0x04 /* Corrected data */
#define ATA_SR_IDX  0x02 /* Index */
#define ATA_SR_ERR  0x01 /* Error */

static void ata_wait_bsy(void) {
    while (sys_ioport_inb(ATA_STATUS) & ATA_SR_BSY);
}

static void ata_wait_drq(void) {
    while (!(sys_ioport_inb(ATA_STATUS) & ATA_SR_DRQ));
}

int ata_init(void) {
    /*
     * 简单的复位/检测逻辑
     * 实际初始化可能需要更复杂的流程,这里假设 BIOS/QEMU 已初始化好
     */
    sys_ioport_outb(ATA_CTRL_COMMAND, 0x02); /* 禁用中断 */
    return 0;
}

bool ata_is_ready(uint8_t drive) {
    sys_ioport_outb(ATA_DRIVE_HEAD, (drive == 0 ? 0xA0 : 0xB0));
    sys_ioport_inb(ATA_STATUS);
    sys_ioport_inb(ATA_STATUS);
    sys_ioport_inb(ATA_STATUS);
    sys_ioport_inb(ATA_STATUS);
    return (sys_ioport_inb(ATA_STATUS) & ATA_SR_DRDY) != 0;
}

int ata_read(uint8_t drive, uint32_t lba, uint32_t count, void *buffer) {
    uint16_t *buf = (uint16_t *)buffer;

    ata_wait_bsy();
    sys_ioport_outb(ATA_DRIVE_HEAD, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    sys_ioport_outb(ATA_SECTOR_COUNT, (uint8_t)count);
    sys_ioport_outb(ATA_LBA_LOW, (uint8_t)lba);
    sys_ioport_outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
    sys_ioport_outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    sys_ioport_outb(ATA_COMMAND, ATA_CMD_READ_PIO);

    for (uint32_t i = 0; i < count; i++) {
        ata_wait_bsy();
        ata_wait_drq();
        for (int j = 0; j < 256; j++) {
            buf[i * 256 + j] = sys_ioport_inw(ATA_DATA);
        }
    }

    return 0;
}

int ata_write(uint8_t drive, uint32_t lba, uint32_t count, const void *buffer) {
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
        for (int j = 0; j < 256; j++) {
            sys_ioport_outw(ATA_DATA, buf[i * 256 + j]);
        }
        sys_ioport_outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
        ata_wait_bsy();
    }

    return 0;
}

uint32_t ata_get_sector_count(uint8_t drive) {
    uint16_t identify_data[256];

    /* 等待驱动器不忙 */
    ata_wait_bsy();

    /* 选择驱动器 */
    sys_ioport_outb(ATA_DRIVE_HEAD, drive == 0 ? 0xA0 : 0xB0);

    /* 发送 IDENTIFY 命令 */
    sys_ioport_outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    /* 等待驱动器响应 */
    ata_wait_bsy();

    /* 检查状态 */
    uint8_t status = sys_ioport_inb(ATA_STATUS);
    if (status == 0 || (status & ATA_SR_ERR)) {
        /* 驱动器不存在或命令失败 */
        return 0;
    }

    /* 等待数据就绪 */
    ata_wait_drq();

    /* 读取 IDENTIFY 数据(256 个 16 位字) */
    for (int i = 0; i < 256; i++) {
        identify_data[i] = sys_ioport_inw(ATA_DATA);
    }

    /*
     * ATA IDENTIFY 返回的扇区数位于:
     * - Word 60-61: 28 位 LBA 可寻址扇区数(旧标准)
     * - Word 100-103: 48 位 LBA 可寻址扇区数(新标准)
     *
     * 我们优先使用 48 位 LBA,如果不支持则使用 28 位 LBA
     */

    /* Word 83 bit 10 表示是否支持 48 位 LBA */
    bool lba48_supported = (identify_data[83] & (1 << 10)) != 0;

    uint32_t sector_count;

    if (lba48_supported) {
        /* 使用 48 位 LBA(Word 100-103),但我们只取低 32 位 */
        sector_count = (uint32_t)identify_data[100] | ((uint32_t)identify_data[101] << 16);
    } else {
        /* 使用 28 位 LBA(Word 60-61) */
        sector_count = (uint32_t)identify_data[60] | ((uint32_t)identify_data[61] << 16);
    }

    return sector_count;
}
