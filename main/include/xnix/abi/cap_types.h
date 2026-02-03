#ifndef _XNIX_ABI_CAP_TYPES_H
#define _XNIX_ABI_CAP_TYPES_H

#include <xnix/abi/types.h>

/* 系统预定义的常用能力槽位 */
enum cap_slot {
    CAP_SLOT_SERIAL_EP = 0,
    CAP_SLOT_IOPORT    = 1,
    CAP_SLOT_VFS_EP    = 2,
    CAP_SLOT_KBD_EP    = 3,
    CAP_SLOT_FB_EP     = 4,
    CAP_SLOT_ROOTFS_EP = 5,
    CAP_SLOT_ATA_IO    = 6,
    CAP_SLOT_ATA_CTRL  = 7,
};

#endif
