/**
 * @file diskio_mem.h
 * @brief diskio 扩展接口: 内存/ATA 设备初始化
 */

#ifndef DISKIO_MEM_H
#define DISKIO_MEM_H

#include <stdint.h>

/**
 * 初始化内存设备
 * 设置后 FatFs 的 disk_* 调用将路由到此内存区域
 */
void disk_init_memory(void *data, uint32_t size);

/**
 * 初始化 ATA 设备 (带分区偏移)
 * 设置后 FatFs 的 disk_* 调用将路由到 ATA + base_lba 偏移
 */
void disk_init_ata(int drive, uint32_t base_lba);

#endif /* DISKIO_MEM_H */
