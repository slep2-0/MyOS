/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		Bugcheck functions implementation.
 */

#include "bugcheck.h"

void bugcheck_system(REGS* registers, BUGCHECK_CODES err_code, uint32_t additional, bool isAdditionals) {
	// Critical system error, instead of triple faulting, we hang the system with specified error codes.
	// Disable interrupts if werent disabled before.
	__cli();

	// Clear the screen to blue (bsod windows style)
	clear_screen(make_color(COLOR_WHITE, COLOR_BLUE));

	// Write some debugging and an error message
	print_to_screen("\r\nFATAL ERROR: Your system has encountered a fatal error.\r\n", COLOR_WHITE);
	print_to_screen("Your system has been stopped for safety.\r\n", COLOR_WHITE);
	print_to_screen("\r\nSTOP_CODE: ", COLOR_WHITE);
	print_dec(err_code, COLOR_YELLOW);
	if (registers) {
		myos_printf(COLOR_WHITE, "\r\n\r\nRegisters:\r\nEAX: %x EBX: %x ECX: %x EDX: %x\r\nESI: %x EDI: %x EBP: %x ESP: %x\r\nDS: %x ES: %x FS: %x GS: %x\r\nEIP: %x CS: %x ELAGS: %x\r\nExceptions: \r\nVector Number: %d Error Number: %x",
			registers->eax,
			registers->ebx,
			registers->ecx,
			registers->edx,
			registers->esi,
			registers->edi,
			registers->ebp,
			registers->esp,
			registers->ds,
			registers->es,
			registers->fs,
			registers->gs,
			registers->eip,
			registers->cs,
			registers->eflags,
			registers->vec_num,
			registers->error_code
		);
	}
	else {
		print_to_screen("\r\n\r\nERROR: NO REGISTERS.", COLOR_RED);
	}
	print_to_screen("\r\n\r\nERROR: NO REGISTERS.", COLOR_RED);
	if (isAdditionals) {
		if (err_code == PAGE_FAULT) {
			print_to_screen("\r\n\r\nFAULTY ADDRESS: ", COLOR_YELLOW);
			print_hex(additional, COLOR_WHITE);
		}
		else {
			print_to_screen("\r\n\r\nADDITIONALS: ", COLOR_YELLOW);
			print_hex(additional, COLOR_WHITE);
		}
	}
	//test
	__hlt();
}