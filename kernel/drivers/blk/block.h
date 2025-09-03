// kernel/drivers/blk/block.h
#ifndef X86_KERNEL_DRIVER_BLK_BLOCK_H
#define X86_KERNEL_DRIVER_BLK_BLOCK_H

// Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../trace.h"
#include "../../bugcheck/bugcheck.h"
#include "../../cpu/cpu.h"

typedef struct _BLOCK_DEVICE {
    bool (*read_sector)(struct _BLOCK_DEVICE* dev,
        uint32_t lba,
        void* buf);
    bool (*write_sector)(struct _BLOCK_DEVICE* dev,
        uint32_t lba,
        const void* buf);
    void* dev_data;
} BLOCK_DEVICE;

/* Register a block device so `get_block_device()` can find it */
void register_block_device(BLOCK_DEVICE* dev);

/* Get the "n" registered device (0, 1, ...), or NULL if out of range. */
BLOCK_DEVICE* get_block_device(int index);

#endif // X86_KERNEL_DRIVER_BLK_BLOCK_H
