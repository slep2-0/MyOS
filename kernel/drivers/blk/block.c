/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Block Device Abstraction Driver Implementation
 */

#include "block.h"
#include "../../includes/me.h"
#include "../../includes/mg.h"

#define MAX_BLK_DEV 32 // AHCI is a maximum of 32, anymore than that and we bugcheck.

static BLOCK_DEVICE* devices[MAX_BLK_DEV];
extern GOP_PARAMS gop_local;
static int device_count = 0;

void register_block_device(BLOCK_DEVICE* dev) {
    // print the index we’re about to use and the device pointer
#ifdef DEBUG
    gop_printf(0xFFFFFF00, "Registering block #%d at %x\n", device_count, (uintptr_t)dev);
#endif
    if (device_count < MAX_BLK_DEV) {
        devices[device_count++] = dev;
    }
    else {
        // too many!
        MeBugCheck(BLOCK_DEVICE_LIMIT_REACHED);
    }
}


BLOCK_DEVICE* get_block_device(int index) {
	if (index < 0 || index >= device_count) { return NULL; }
	return devices[index];
}
