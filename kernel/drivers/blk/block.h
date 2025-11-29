// kernel/drivers/blk/block.h
#ifndef X86_KERNEL_DRIVER_BLK_BLOCK_H
#define X86_KERNEL_DRIVER_BLK_BLOCK_H

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../mtstatus.h"

typedef struct _BLOCK_DEVICE {
    MTSTATUS(*read_sector)(struct _BLOCK_DEVICE* dev,
        uint32_t lba,
        void* buf,
        size_t bytes);
    MTSTATUS(*write_sector)(struct _BLOCK_DEVICE* dev,
        uint32_t lba,
        const void* buf,
        size_t bytes);
    void* dev_data;
} BLOCK_DEVICE;

/* Register a block device so `get_block_device()` can find it */
void register_block_device(BLOCK_DEVICE* dev);

/* Get the "n" registered device (0, 1, ...), or NULL if out of range. */
BLOCK_DEVICE* get_block_device(int index);

#endif // X86_KERNEL_DRIVER_BLK_BLOCK_H
