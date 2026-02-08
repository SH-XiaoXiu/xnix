/**
 * @file diskio.c
 * @brief FatFs 块设备接口适配层
 *
 * 实现 FatFs 的 diskio 接口, 支持两种后端:
 * - 内存模式: 从 module_system mmap 的内存区域
 * - ATA 模式: ATA PIO 磁盘 (带分区偏移)
 *
 * FatFs 始终使用 pdrv=0 (FF_VOLUMES=1), diskio 内部根据模式路由.
 */

#include "ata.h"
#include "diskio_mem.h"

// clang-format off
#include <ff.h>      // 必须先于 diskio.h,定义 BYTE/UINT/LBA_t 等类型
#include <diskio.h>
// clang-format on

#include <string.h>

/* 设备模式 */
#define DISK_MODE_NONE   0
#define DISK_MODE_MEMORY 1
#define DISK_MODE_ATA    2

static int g_mode;

/* 内存设备状态 */
static uint8_t *g_mem_data;
static uint32_t g_mem_size;

/* ATA 设备状态 */
static int      g_ata_drive;
static uint32_t g_ata_base_lba;

void disk_init_memory(void *data, uint32_t size) {
    g_mem_data = (uint8_t *)data;
    g_mem_size = size;
    g_mode     = DISK_MODE_MEMORY;
}

void disk_init_ata(int drive, uint32_t base_lba) {
    g_ata_drive    = drive;
    g_ata_base_lba = base_lba;
    g_mode         = DISK_MODE_ATA;
}

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    switch (g_mode) {
    case DISK_MODE_MEMORY:
        return 0;
    case DISK_MODE_ATA:
        return ata_is_ready(g_ata_drive) ? 0 : STA_NOINIT;
    default:
        return STA_NOINIT;
    }
}

DSTATUS disk_initialize(BYTE pdrv) {
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;

    if (g_mode == DISK_MODE_MEMORY) {
        uint32_t offset = (uint32_t)sector * ATA_SECTOR_SIZE;
        uint32_t len    = count * ATA_SECTOR_SIZE;
        if (offset + len > g_mem_size) {
            return RES_PARERR;
        }
        memcpy(buff, g_mem_data + offset, len);
        return RES_OK;
    }

    if (g_mode == DISK_MODE_ATA) {
        if (!ata_is_ready(g_ata_drive)) {
            return RES_NOTRDY;
        }
        if (ata_read(g_ata_drive, sector + g_ata_base_lba, count, buff) < 0) {
            return RES_ERROR;
        }
        return RES_OK;
    }

    return RES_NOTRDY;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;

    if (g_mode == DISK_MODE_MEMORY) {
        uint32_t offset = (uint32_t)sector * ATA_SECTOR_SIZE;
        uint32_t len    = count * ATA_SECTOR_SIZE;
        if (offset + len > g_mem_size) {
            return RES_PARERR;
        }
        memcpy(g_mem_data + offset, buff, len);
        return RES_OK;
    }

    if (g_mode == DISK_MODE_ATA) {
        if (!ata_is_ready(g_ata_drive)) {
            return RES_NOTRDY;
        }
        if (ata_write(g_ata_drive, sector + g_ata_base_lba, count, buff) < 0) {
            return RES_ERROR;
        }
        return RES_OK;
    }

    return RES_NOTRDY;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv;

    if (g_mode == DISK_MODE_MEMORY) {
        switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = g_mem_size / ATA_SECTOR_SIZE;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = ATA_SECTOR_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;
            return RES_OK;
        default:
            return RES_PARERR;
        }
    }

    if (g_mode == DISK_MODE_ATA) {
        if (!ata_is_ready(g_ata_drive)) {
            return RES_NOTRDY;
        }
        switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = ata_get_sector_count(g_ata_drive);
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = ATA_SECTOR_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;
            return RES_OK;
        default:
            return RES_PARERR;
        }
    }

    return RES_NOTRDY;
}
