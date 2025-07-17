/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Core Kernel for MatanelOS.
 */
#include "kernel.h"

void kernel_main(void) {
	print_to_screen("Kernel Reached.\n\r", COLOR_GREEN);
	__hlt();
	// END of the kernel, should never be reached on a normal basis, if it reaches here the cpu halts. -> hlt
}