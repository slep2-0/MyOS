/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Bugcheck functions implementation.
 */

#include "bugcheck.h"

extern GOP_PARAMS* gop; // GOP.

void bugcheck_system(REGS* registers, BUGCHECK_CODES err_code, uint32_t additional, bool isAdditionals) {
	// Critical system error, instead of triple faulting, we hang the system with specified error codes.
	// Disable interrupts if werent disabled before.
	UNREFERENCED_PARAMETER(additional);
	UNREFERENCED_PARAMETER(isAdditionals);
	UNREFERENCED_PARAMETER(registers);
	if (err_code == SEVERE_MACHINE_CHECK) {
		for (uint32_t row = 0; row < gop->Height; row++) {
			for (uint32_t col = 0; col < gop->Width; col++) {
				plot_pixel(gop, col, row, 0xFFFF0000);  // Solid RED
			}
		}
	}
	else if (err_code == BAD_PAGING) {
		for (uint32_t row = 0; row < gop->Height; row++) {
			for (uint32_t col = 0; col < gop->Width; col++) {
				plot_pixel(gop, col, row, 0xFF0000FF);  // Solid blue
			}
		}
		draw_string(gop, "Hello People!\n", 10, 10, 0xFFFFFFFF);
	}
	else {
		for (uint32_t row = 0; row < gop->Height; row++) {
			for (uint32_t col = 0; col < gop->Width; col++) {
				plot_pixel(gop, col, row, 0xFFFFFF00);  // Solidsomething
			}
		}
	}
	__cli();
	__hlt();
}