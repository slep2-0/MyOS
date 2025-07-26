/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Block Device Abstraction Driver Implementation
 */

#include "block.h"

#define MAX_BLK_DEV 4

static BLOCK_DEVICE* devices[MAX_BLK_DEV];
extern GOP_PARAMS gop_local;
static int device_count = 0;

void register_block_device(BLOCK_DEVICE* dev) {
    tracelast_func("register_block_device");
    // print the index we’re about to use and the device pointer
#ifdef DEBUG
    gop_printf(&gop_local, 0xFFFFFF00, "Registering block #%d at %x\n", device_count, (unsigned)dev);
#endif
    if (device_count < MAX_BLK_DEV) {
        devices[device_count++] = dev;
    }
    else {
        // too many!
        bugcheck_system(NULL, BLOCK_DEVICE_LIMIT_REACHED, 0, false);
    }
}


BLOCK_DEVICE* get_block_device(int index) {
    tracelast_func("get_block_device");
	if (index < 0 || index >= device_count) { return NULL; }
	return devices[index];
}