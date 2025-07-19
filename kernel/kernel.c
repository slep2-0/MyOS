/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel Entry Point for MatanelOS.
 */
#include "kernel.h"

// Small note, now that paging is enabled (gotta remind myself that), the " . " in the linker (dot), resembles virtual addresses now, and NOT physical (while the kernel is still stored at lets say 0x10000 or something, just right after bootloader 2.
// we can still map the kernel to virtual address 0xC000000 (higher limit of 2GB) to 0xFFFFFFF, using the dot, (so setting . to that 0xC000 thing, then for looping all the way to 0xFFF(f), to map the page as reserved, and KM only)

void kernel_main(void) {
	zero_bss();
	// Clear the screen.
	clear_screen(COLOR_BLACK); 
	// Initialize our hardware interrupts.
	init_interrupts();
	// Initialize our virtual memory paging.
	paging_init();
	// Initialize Frame Bitmap.
	frame_bitmap_init();
	// Initialize our heap.
	init_heap();
	// Initialize our ATA device so disk 0 exists.
	ata_init_primary();
	// Initialize the timer to 100Mhz
	init_timer(100);
	// Initialize keyboard, will set all booleans to false (ctrl,shift,caps)
	init_keyboard();
	//if (fat32_init(0)) {
		//fat32_list_root();
	//}
	// Read the DISK.
	/*
	BLOCK_DEVICE* disk = get_block_device(0);
	uint8_t buffer[512];

	for (uint32_t sector = 0; sector < 10; sector++) {
		if (!disk->read_sector(disk, sector, buffer)) {
			myos_printf(COLOR_RED, "Failed reading sector %d\r\n", sector);
			break;
		}
		else {
			myos_printf(COLOR_CYAN, "Sector %d data:\r\n", sector);
			for (int i = 0; i < 512; i++) {
				// Print hex byte with a space for readability
				myos_printf(COLOR_LIGHT_GRAY, "%x ", buffer[i]);

				// Optional: newline every 16 bytes for better formatting
				if ((i + 1) % 16 == 0) {
					print_to_screen("\r\n", COLOR_LIGHT_GRAY);
				}
			}
		}
	}
	__cli();
	__hlt();
	*/
	// Move the VGA hardware cursor out of the way, we are using a software cursor
	set_hardware_cursor_position(500, 500);
	// Debug Prints.
	print_to_screen("Kernel Reached.\r\n", COLOR_GREEN);
	print_to_screen("Enabling Interrupts...\r\n", COLOR_CYAN);
	print_to_screen("Initiated Heap and timers.\r\n", COLOR_RED);
	//Allocate 256 -- 16 byte alignment.
	void* buf = kmalloc(64, 16);
	print_hex((unsigned int)(uintptr_t)buf, COLOR_WHITE);
	print_to_screen(" <- allocated 64 bytes, aligned to 16 byte groups.\r\n", COLOR_LIGHT_GREEN);
	kfree(buf);
	// reallocate to see if it reallocates back to the same addr.
	void* buf2 = kmalloc(64, 16);
	print_hex((unsigned int)(uintptr_t)buf2, COLOR_WHITE);
	print_to_screen(" <- allocated 64 bytes, aligned to 16 byte groups. (number 2, should allocate to same addr)\r\n", COLOR_LIGHT_GREEN);
	// Test dynamic memory allocation.
	for (int i = 0; i < 50; i++) {
		kmalloc(64, 16);
	}
	void* buf3 = kmalloc(64, 16);
	if (buf3) {
		myos_printf(COLOR_BROWN, "BUF3 ADDR: %x", buf3);
	}
	else {
		myos_printf(COLOR_RED, "No BUF3");
	}
#ifdef CAUSE_BUGCHECK
	bugcheck_system(NULL, MANUALLY_INITIATED_CRASH, 0xDEADBEEF, true);
#endif
	while (1) {
		// Keep kernel ALWAYS running, while loop.
		// HALT Instruction will halt the CPU until the next interrupt occurs.
		__hlt();
	}
}