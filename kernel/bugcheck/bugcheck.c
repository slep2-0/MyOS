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
		print_to_screen("\r\n\r\nRegisters:\r\n", COLOR_WHITE);
		print_to_screen("EAX: ", COLOR_WHITE);
		print_hex(registers->eax, COLOR_WHITE);
		print_to_screen(" EBX: ", COLOR_WHITE);
		print_hex(registers->ebx, COLOR_WHITE);
		print_to_screen(" ECX: ", COLOR_WHITE);
		print_hex(registers->ecx, COLOR_WHITE);
		print_to_screen(" EDX: ", COLOR_WHITE);
		print_hex(registers->edx, COLOR_WHITE);
		print_to_screen("\r\nESI: ", COLOR_WHITE);
		print_hex(registers->esi, COLOR_WHITE);
		print_to_screen(" EDI: ", COLOR_WHITE);
		print_hex(registers->edi, COLOR_WHITE);
		print_to_screen(" EBP: ", COLOR_WHITE);
		print_hex(registers->ebp, COLOR_WHITE);
		print_to_screen(" ESP: ", COLOR_WHITE);
		print_hex(registers->esp, COLOR_WHITE);
		print_to_screen("\r\nDS: ", COLOR_WHITE);
		print_hex(registers->ds, COLOR_WHITE);
		print_to_screen(" ES: ", COLOR_WHITE);
		print_hex(registers->es, COLOR_WHITE);
		print_to_screen(" FS: ", COLOR_WHITE);
		print_hex(registers->fs, COLOR_WHITE);
		print_to_screen(" GS: ", COLOR_WHITE);
		print_hex(registers->gs, COLOR_WHITE);
		print_to_screen("\r\nEIP: ", COLOR_WHITE);
		print_hex(registers->eip, COLOR_WHITE);
		print_to_screen(" CS: ", COLOR_WHITE);
		print_hex(registers->cs, COLOR_WHITE);
		print_to_screen(" EFLAGS: ", COLOR_WHITE);
		print_hex(registers->eflags, COLOR_WHITE);
		print_to_screen("\r\nExceptions: ", COLOR_WHITE);
		print_to_screen("\r\nVector Number: ", COLOR_WHITE);
		print_dec(registers->vec_num, COLOR_WHITE);
		print_to_screen(" Error Number: ", COLOR_WHITE);
		print_hex(registers->error_code, COLOR_WHITE);
	}
	else {
		print_to_screen("ERROR: NO REGISTERS.", COLOR_RED);
	}
	if (isAdditionals) {
		print_to_screen("\r\n\r\nADDITIONALS: ", COLOR_YELLOW);
		print_hex(additional, COLOR_WHITE);
	}
	//test
	__hlt();
}