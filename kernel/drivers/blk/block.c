/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Block Device Abstraction Driver Implementation
 */

#include "block.h"

#define MAX_BLK_DEV 4

static BLOCK_DEVICE* devices[MAX_BLK_DEV];
static int device_count = 0;

#include <stddef.h>                   // for NULL
#include "../../screen/vga/vga.h"          // for myos_printf and COLOR_YELLOW
#include "../../bugcheck/bugcheck.h"       // for bugcheck_system and BLOCK_DEVICE_LIMIT_REACHED

void register_block_device(BLOCK_DEVICE* dev) {
    // print the index we’re about to use and the device pointer
    myos_printf(COLOR_YELLOW,
        "Registering block #%d at %x\r\n",
        device_count,
        (unsigned)dev);

    if (device_count < MAX_BLK_DEV) {
        devices[device_count++] = dev;
    }
    else {
        // too many!
        bugcheck_system(NULL, BLOCK_DEVICE_LIMIT_REACHED, 0, false);
    }
}


BLOCK_DEVICE* get_block_device(int index) {
	if (index < 0 || index >= device_count) { return NULL; }
	return devices[index];
}