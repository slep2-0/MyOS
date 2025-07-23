/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      NONE
 * PURPOSE:      Core Kernel Entry Point for MatanelOS.
 */
#include "kernel.h"

 // Small note, now that paging is enabled (gotta remind myself that), the " . " in the linker (dot), resembles virtual addresses now, and NOT physical (while the kernel is still stored at lets say 0x10000 or something, just right after bootloader 2.
 // we can still map the kernel to virtual address 0xC000000 (higher limit of 2GB) to 0xFFFFFFF, using the dot, (so setting . to that 0xC000 thing, then for looping all the way to 0xFFF(f), to map the page as reserved, and KM only)

void kernel_main(GOP_PARAMS* gop) {
    // --- 1. CORE SYSTEM INITIALIZATION ---
    // All system services MUST be initialized before we can use them.
    // Paging is especially critical as it allows access to kernel data.
    zero_bss();
    // Initialize our virtual memory paging.
    paging_init();
    // Initialize our hardware interrupts.
    init_interrupts();
    // Initialize Frame Bitmap.
    frame_bitmap_init();
    // Initialize our heap.
    init_heap();
    // Initialize our ATA device so disk 0 exists.
    ata_init_primary();
    // Initialize the timer to 100Mhz
    init_timer(100);
    // Initialize keyboard, will set all booleans to false (ctrl,shift,caps)
    // init_keyboard(); // Commented out, it uses VGA.

    // --- 2. GRAPHICS AND APPLICATION LOGIC ---
    // Now that the system is initialized, we can safely access the framebuffer
    // and kernel data (like the font table).
    for (uint32_t row = 0; row < gop->Height; row++) {
        for (uint32_t col = 0; col < gop->Width; col++) {
            plot_pixel(gop, col, row, 0x00000000);  // Black background
        }
    }

    const uint8_t* bitmap = font8x8_basic['H' - 32];
    uint8_t first_row = bitmap[0]; // Should be 0x33 for 'H'

    // Draw each bit of the first row as a big square
    for (int bit = 0; bit < 8; bit++) {
        if (first_row & (1 << (7 - bit))) {
            // Draw a 10x10 white square for each set bit
            for (int x = 0; x < 10; x++) {
                for (int y = 0; y < 10; y++) {
                    plot_pixel(gop, 300 + bit * 12 + x, 300 + y, 0xFFFFFFFF);
                }
            }
        }
    }

    draw_string(gop, "Hello MatanelOS! Graphics are working!", 50, 50, 0x00FFFFFF);


    //if (fat32_init(0)) {
        //fat32_list_root();
    //}
    // Read the DISK.
    /*
    BLOCK_DEVICE* disk = get_block_device(0);
    uint8_t buffer[512];

    for (uint32_t sector = 0; sector < 10; sector++) {
        if (!disk->read_sector(disk, sector, buffer)) {
            // No printing functions available in GOP mode yet
        }
        else {
            // No printing functions available in GOP mode yet
        }
    }
    __cli();
    __hlt();
    */

#ifdef CAUSE_BUGCHECK
    bugcheck_system(NULL, MANUALLY_INITIATED_CRASH, 0xDEADBEEF, true);
#endif

    // --- 3. FINAL KERNEL IDLE LOOP ---
    // The kernel has finished its setup and primary tasks.
    // It now halts the CPU, waiting for interrupts to occur.
    while (1) {
        // Keep kernel ALWAYS running, while loop.
        // HALT Instruction will halt the CPU until the next interrupt occurs.
        __outbyte(0xE9, 0x42);  // 0xE9 is often used for debug output in emulators
        __hlt();
    }
}