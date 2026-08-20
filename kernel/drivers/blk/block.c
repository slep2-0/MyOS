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

void register_block_device(BLOCK_DEVICE* dev)

/*++

    Routine description:

        Registers a block-device interface in the global device table.

    Arguments:

        [IN] dev - Block device used for the transfer.

    Return Values:

        None.

--*/

{
    // print the index we’re about to use and the device pointer
#ifdef DEBUG
    gop_printf(0xFFFFFF00, "Registering block #%d at %llx\n", device_count, (unsigned long long)(uintptr_t)dev);
#endif
    if (device_count < MAX_BLK_DEV) {
        devices[device_count++] = dev;
    }
    else {
        // too many!
        MeBugCheck(BLOCK_DEVICE_LIMIT_REACHED);
    }
}


BLOCK_DEVICE* get_block_device(int index)

/*++

    Routine description:

        Returns the block device registered at an index.

    Arguments:

        [IN] index - Index of the entry to process.

    Return Values:

        A pointer to the resulting object or storage, or NULL when no result is available.

--*/

{
	if (index < 0 || index >= device_count) { return NULL; }
	return devices[index];
}
