/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel for MatanelOS.
 */
#include "kernel.h"

void kernel_main(void) {
	clear_screen(COLOR_BLACK);
	init_timer(1);
	set_hardware_cursor_position(500, 500);
	print_to_screen("Kernel Reached.\r\n", COLOR_GREEN);
	print_to_screen("Enabling Interrupts...\r\n", COLOR_CYAN);
	init_interrupts();
	while (1) {
		// Keep kernel ALWAYS running, while loop.
		// HALT Instruction will halt the CPU until the next interrupt occurs.
		__hlt();
	}
}