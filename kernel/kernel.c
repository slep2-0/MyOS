/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel for MatanelOS.
 */
#include "kernel.h"

void kernel_main(void) {
	//print_to_screen(text, make_color(0xF, 0x0));
	clear_screen(COLOR_BLACK);
	char* text = "Happy Birthday Dvir Ya Zona";
	print_to_screen(text, COLOR_GREEN);
	// END of the kernel, should never be reached on a normal basis, if it reaches here the cpu halts. -> hlt
}