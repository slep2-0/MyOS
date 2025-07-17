/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel for MatanelOS.
 */
#include "kernel.h"

void kernel_main(void) {
	clear_screen(COLOR_BLACK);
	init_heap();      
	init_interrupts();
	init_timer(100);  
	set_hardware_cursor_position(500, 500);
	print_to_screen("Kernel Reached.\r\n", COLOR_GREEN);
	print_to_screen("Enabling Interrupts...\r\n", COLOR_CYAN);
	print_to_screen("Initiated Heap and timers.\r\n", COLOR_RED);
	//Allocate 256 -- 16 byte alignment.
	void* buf = kmalloc(64, 16);
	print_hex((unsigned int)(uintptr_t)buf, COLOR_WHITE);
	print_to_screen(" <- allocated 64 bytes, aligned to 16 byte groups.\r\n", COLOR_YELLOW);
	kfree(buf);
	// reallocate to see if it reallocates back to the same addr.
	void* buf2 = kmalloc(64, 16);
	print_hex((unsigned int)(uintptr_t)buf2, COLOR_WHITE);
	print_to_screen(" <- allocated 64 bytes, aligned to 16 byte groups. (number 2, should allocate to same addr)\r\n", COLOR_YELLOW);
	void* buf3 = kmalloc(64, 16);
	print_hex((unsigned int)(uintptr_t)buf3, COLOR_WHITE);
	print_to_screen(" <- allocated 64 bytes, aligned to 16 byte groups. (number 3 - new address. 64 bytes + sizeof(BLOCK_HEADER) = 72 bytes.)\r\n", COLOR_YELLOW);
	myos_printf(COLOR_GREEN, "This is the number: %d, and string: %s, and mem address: %x\r\n", 0, 0, buf3);
	while (1) {
		// Keep kernel ALWAYS running, while loop.
		// HALT Instruction will halt the CPU until the next interrupt occurs.
		__hlt();
	}
}