/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel Entry Point for MatanelOS.
 */
#include "kernel.h"

void kernel_main(void) {
	// Clear the screen.
	clear_screen(COLOR_BLACK);
	// Initialize our heap.
	init_heap();      
	// Initialize our hardware interrupts.
	init_interrupts();
	// Initialize our virtual memory paging.
	paging_init();
	// Initialize the timer to 100Mhz
	init_timer(100);
	// Initialize keyboard, will set all booleans to false (ctrl,shift,caps)
	init_keyboard();
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
	void* buf3 = kmalloc(64, 16);
	print_hex((unsigned int)(uintptr_t)buf3, COLOR_WHITE);
	print_to_screen(" <- allocated 64 bytes, aligned to 16 byte groups. (number 3 - new address. 64 bytes + sizeof(BLOCK_HEADER) = 72 bytes.)\r\n", COLOR_LIGHT_GREEN);
	myos_printf(COLOR_GREEN, "This is the number: %d, and string: %s, and mem address: %x\r\n", 0, 0, buf3);
#ifdef CAUSE_BUGCHECK
	int* test = (int*)0xFFFFFFFF;
	*test = 1;  // Should cause a page fault if unmapped
#endif
	while (1) {
		// Keep kernel ALWAYS running, while loop.
		// HALT Instruction will halt the CPU until the next interrupt occurs.
		__hlt();
	}
}