/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     ATA PI/O Driver API Header.
 */
#ifndef X86_KERNEL_DRIVER_BLK_ATA_H
#define X86_KERENL_DRIVER_BLK_ATA_H
#include "block.h"

// Probe the primary ATA channel and register a block device for the HDA if found.
void ata_init_primary(void);

#endif
