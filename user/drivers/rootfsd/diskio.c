/**
 * @file diskio.c
 * @brief FatFs diskio 适配层 - 内存 FAT 镜像版本
 *
 * 从内存中的 FAT 镜像读取数据,用于 rootfs
 */

#include "diskio.h"

#include "ff.h"

#include <string.h>

/* FAT 镜像信息 */
static const uint8_t *g_fat_image = NULL;
static uint32_t       g_fat_size  = 0;

/**
 * 初始化内存 FAT 镜像
 */
void diskio_set_image(const void *image, uint32_t size) {
    g_fat_image = (const uint8_t *)image;
    g_fat_size  = size;
}

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    return (g_fat_image != NULL) ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;

    if (g_fat_image == NULL) {
        return RES_NOTRDY;
    }

    uint32_t offset = sector * 512;
    uint32_t size   = count * 512;

    if (offset + size > g_fat_size) {
        return RES_PARERR;
    }

    memcpy(buff, g_fat_image + offset, size);
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    (void)pdrv;
    (void)buff;
    (void)sector;
    (void)count;
    /* rootfs 是只读的 */
    return RES_WRPRT;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv;

    if (g_fat_image == NULL) {
        return RES_NOTRDY;
    }

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;

    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = g_fat_size / 512;
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
