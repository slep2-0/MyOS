/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      NONE
 * PURPOSE:      Core Kernel Entry Point for MatanelOS.
 */

#include "kernel.h"

GOP_PARAMS gop_local;
BOOT_INFO boot_info_local;

#define MAX_MEMORY_MAP_SIZE 0x4000  // 16 KB, enough for ~256 descriptors

static EFI_MEMORY_DESCRIPTOR memory_map_copy[MAX_MEMORY_MAP_SIZE / sizeof(EFI_MEMORY_DESCRIPTOR)];

void copy_memory_map(BOOT_INFO* boot_info) {
    if (!boot_info || !boot_info->MemoryMap) return;

    size_t count = boot_info->MapSize / boot_info->DescriptorSize;
    if (boot_info->MapSize > MAX_MEMORY_MAP_SIZE) {
        // handle error, memory map too big
        bugcheck_system(NULL, MEMORY_MAP_SIZE_OVERRUN, 0, false);
    }

    // Copy the entire memory map into the static buffer
    kmemcpy(memory_map_copy, boot_info->MemoryMap, boot_info->MapSize);

    boot_info_local.MemoryMap = memory_map_copy;
    boot_info_local.MapSize = boot_info->MapSize;
    boot_info_local.DescriptorSize = boot_info->DescriptorSize;
    boot_info_local.DescriptorVersion = boot_info->DescriptorVersion;
}

void copy_gop(BOOT_INFO* boot_info) {
    if (!boot_info || !boot_info->Gop) return;

    // Copy the GOP data to a local global variable
    gop_local = *(boot_info->Gop);

    // Update all relevant pointers to point to the local copy
    boot_info->Gop = &gop_local;
    boot_info_local.Gop = &gop_local;
}


void init_boot_info(BOOT_INFO* boot_info) {
    if (!boot_info) return;

    copy_memory_map(boot_info);
    copy_gop(boot_info);
}


// Simple serial debug helper
void debug_print(const char* s) {
    while (*s) __outbyte(0x402, *s++);
}

void kernel_main(BOOT_INFO* boot_info) {
    // 1. CORE SYSTEM INITIALIZATION
    __cli();
    zero_bss();

    init_boot_info(boot_info);
    frame_bitmap_init();
    gop_clear_screen(&gop_local, 0); // 0 is just black. (0x0000000)
    paging_init();
    init_interrupts();
    //__sti();           // only now enable interrupts -- not yet, gotta setup keyboard, exceptions are fine tho.
    gop_printf(&gop_local, 0xFFFF0000, "Hello People! Number: %d , String: %s , HEX: %p\n", 5, "MyOS!", 0x123123);
    gop_printf(&gop_local, 0xFF0000FF, "Testing! %d %d %d", 1, 2, 3);
    __hlt(); // Wait forever
    //init_heap();
    //ata_init_primary();
    //init_timer(100);
    // init_keyboard(); // optional

#ifdef CAUSE_BUGCHECK
    bugcheck_system(NULL, MANUALLY_INITIATED_CRASH, 0xDEADBEEF, true);
#endif

    // 3. FINAL KERNEL IDLE LOOP
    while (1) {
        // until next interrupt.
        __hlt();
    }
}
