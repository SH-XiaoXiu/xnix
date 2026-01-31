/**
 * @file diskio.c
 * @brief FatFs 块设备接口适配层
 *
 * 实现 FatFs 的 diskio 接口,连接到 ATA 驱动.
 */

#include "ata.h"

#include <diskio.h>
#include <ff.h>

/* 物理驱动器映射 */
#define DEV_ATA0 0 /* ATA 主盘 */
#define DEV_ATA1 1 /* ATA 从盘 */

DSTATUS disk_status(BYTE pdrv) {
    switch (pdrv) {
    case DEV_ATA0:
        return ata_is_ready(0) ? 0 : STA_NOINIT;
    case DEV_ATA1:
        return ata_is_ready(1) ? 0 : STA_NOINIT;
    default:
        return STA_NOINIT;
    }
}

DSTATUS disk_initialize(BYTE pdrv) {
    /* ATA 在 ata_init() 中已初始化 */
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    int drive;

    switch (pdrv) {
    case DEV_ATA0:
        drive = 0;
        break;
    case DEV_ATA1:
        drive = 1;
        break;
    default:
        return RES_PARERR;
    }

    if (!ata_is_ready(drive)) {
        return RES_NOTRDY;
    }

    if (ata_read(drive, sector, count, buff) < 0) {
        return RES_ERROR;
    }

    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    int drive;

    switch (pdrv) {
    case DEV_ATA0:
        drive = 0;
        break;
    case DEV_ATA1:
        drive = 1;
        break;
    default:
        return RES_PARERR;
    }

    if (!ata_is_ready(drive)) {
        return RES_NOTRDY;
    }

    if (ata_write(drive, sector, count, buff) < 0) {
        return RES_ERROR;
    }

    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    int drive;

    switch (pdrv) {
    case DEV_ATA0:
        drive = 0;
        break;
    case DEV_ATA1:
        drive = 1;
        break;
    default:
        return RES_PARERR;
    }

    if (!ata_is_ready(drive)) {
        return RES_NOTRDY;
    }

    switch (cmd) {
    case CTRL_SYNC:
        /* ATA PIO 是同步的,无需额外操作 */
        return RES_OK;

    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = ata_get_sector_count(drive);
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = ATA_SECTOR_SIZE;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1; /* 1 扇区 */
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
