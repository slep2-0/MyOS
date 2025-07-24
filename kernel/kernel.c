/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      NONE
 * PURPOSE:      Core Kernel Entry Point for MatanelOS.
 */

#include "kernel.h"

 // Globals for the UEFI memory map and GOP
EFI_MEMORY_DESCRIPTOR* gEfiMemoryMap = NULL;
size_t                gEfiMemoryMapSize = 0;
size_t                gEfiDescriptorSize = 0;
GOP_PARAMS* gop;

// Initialize boot info pointers
void init_boot_info(BOOT_INFO* boot_info) {
    if (!boot_info) return;
    gEfiMemoryMap = boot_info->MemoryMap;
    gEfiMemoryMapSize = boot_info->MapSize;
    gEfiDescriptorSize = boot_info->DescriptorSize;
    gop = boot_info->Gop;
}

// Simple serial debug helper
void debug_print(const char* s) {
    while (*s) __outbyte(0x402, *s++);
}

void kernel_main(BOOT_INFO* boot_info) {
    // 1. CORE SYSTEM INITIALIZATION
    zero_bss();

    debug_print("Hello!\n");

    init_boot_info(boot_info);

    // Clear screen via GOP
    for (uint32_t y = 0; y < gop->Height; y++)
        for (uint32_t x = 0; x < gop->Width; x++)
            plot_pixel(gop, x, y, 0x00000000);

    draw_string(gop, "It works!", 10, 10, 0xFFFFFFFF);

    frame_bitmap_init();
    paging_init();
    init_interrupts();
    init_heap();
    ata_init_primary();
    init_timer(100);
    // init_keyboard(); // optional

    // 2. GRAPHICS AND APPLICATION LOGIC
    for (uint32_t row = 0; row < gop->Height; row++)
        for (uint32_t col = 0; col < gop->Width; col++)
            plot_pixel(gop, col, row, 0x00000000);

    draw_string(gop, "Hello MatanelOS! Graphics are working!", 50, 50, 0x00FFFFFF);

#ifdef CAUSE_BUGCHECK
    bugcheck_system(NULL, MANUALLY_INITIATED_CRASH, 0xDEADBEEF, true);
#endif

    // 3. FINAL KERNEL IDLE LOOP
    while (1) {
        __hlt();
    }
}
