/**
 * @file xnix/protocol/blk.h
 * @brief Block Device IPC Protocol
 *
 * Defines IPC protocol for raw block device access and device registration.
 * Used between block device drivers (fatfsd) and device filesystem (devfsd).
 *
 * Opcode 范围 100-199 用于块设备操作, 200+ 用于 devfs 注册协议.
 */

#ifndef XNIX_PROTOCOL_BLK_H
#define XNIX_PROTOCOL_BLK_H

#include <stdint.h>

/* ============== 块设备操作协议 (driver 端处理) ============== */

/**
 * BLK_READ - 读取扇区
 *
 * Request:
 *   regs[0] = UDM_BLK_READ
 *   regs[1] = lba_lo (uint32)
 *   regs[2] = lba_hi (uint32)
 *   regs[3] = sector_count
 *
 * Reply:
 *   regs[1] = 读取的字节数 (>0) 或错误码 (<0)
 *   buffer  = 扇区数据
 */
#define UDM_BLK_READ  100

/**
 * BLK_WRITE - 写入扇区
 *
 * Request:
 *   regs[0] = UDM_BLK_WRITE
 *   regs[1] = lba_lo (uint32)
 *   regs[2] = lba_hi (uint32)
 *   regs[3] = sector_count
 *   buffer  = 扇区数据
 *
 * Reply:
 *   regs[1] = 写入的字节数 (>0) 或错误码 (<0)
 */
#define UDM_BLK_WRITE 101

/**
 * BLK_INFO - 获取设备信息
 *
 * Request:
 *   regs[0] = UDM_BLK_INFO
 *
 * Reply:
 *   regs[1] = 0 (成功) 或错误码
 *   regs[2] = sector_count_lo
 *   regs[3] = sector_count_hi
 *   regs[4] = sector_size (字节)
 *   buffer  = 设备名 (null-terminated)
 */
#define UDM_BLK_INFO  102

/* ============== devfs 注册协议 (devfsd 端处理) ============== */

/**
 * DEVFS_REGISTER_BLOCK - 向 devfsd 注册块设备
 *
 * 由块设备驱动 (如 fatfsd) 在就绪后发送给 devfsd.
 *
 * Request:
 *   regs[0] = UDM_DEVFS_REGISTER_BLOCK
 *   regs[1] = sector_count_lo
 *   regs[2] = sector_count_hi
 *   regs[3] = sector_size
 *   regs[4] = dev_name_len (设备名长度, 不含 \0)
 *   buffer  = [dev_name \0] [mbr_sector 512B]
 *             buffer 总大小 = dev_name_len + 1 + 512
 *   handles[0] = 块设备 endpoint (用于后续 BLK_READ/WRITE)
 *
 * Reply:
 *   regs[1] = 0 (成功) 或错误码
 */
#define UDM_DEVFS_REGISTER_BLOCK 200

/* 块 IO 缓冲区上限 (单次 IPC 传输的最大字节数) */
#define BLK_IO_BUF_SIZE 4096

/* 单次 BLK_READ/WRITE 最大扇区数 (假设 512B/sector) */
#define BLK_IO_MAX_SECTORS (BLK_IO_BUF_SIZE / 512)

#endif /* XNIX_PROTOCOL_BLK_H */
