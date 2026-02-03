/**
 * @file ata.h
 * @brief ATA PIO 驱动接口
 */

#ifndef ATA_H
#define ATA_H

#include <stdbool.h>
#include <stdint.h>

#define ATA_SECTOR_SIZE 512

/**
 * 初始化 ATA 驱动
 *
 * @param io_cap   数据端口 capability (0x1F0-0x1F7)
 * @param ctrl_cap 控制端口 capability (0x3F6-0x3F7)
 * @return 0 成功,负数失败
 */
int ata_init(uint32_t io_cap, uint32_t ctrl_cap);

/**
 * 检查磁盘是否就绪
 *
 * @param drive 驱动器号 (0=主盘, 1=从盘)
 * @return true 就绪,false 未就绪
 */
bool ata_is_ready(uint8_t drive);

/**
 * 读取扇区
 *
 * @param drive  驱动器号
 * @param lba    起始扇区号
 * @param count  扇区数
 * @param buffer 数据缓冲区
 * @return 0 成功,负数失败
 */
int ata_read(uint8_t drive, uint32_t lba, uint32_t count, void *buffer);

/**
 * 写入扇区
 *
 * @param drive  驱动器号
 * @param lba    起始扇区号
 * @param count  扇区数
 * @param buffer 数据缓冲区
 * @return 0 成功,负数失败
 */
int ata_write(uint8_t drive, uint32_t lba, uint32_t count, const void *buffer);

/**
 * 获取磁盘扇区总数
 *
 * @param drive 驱动器号
 * @return 扇区数,0 表示磁盘不存在
 */
uint32_t ata_get_sector_count(uint8_t drive);

#endif /* ATA_H */
