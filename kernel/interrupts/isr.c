/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:		IMPLEMENTATION To SETUP ISR Handler.
 * EXPLANATION: An ISR is what handles the interrupts that gets sent from the CPU (after interrupt is sent to ISR itself), it will do stuff based if it's an exception, or a normal interrupt.
 */

#include "idt.h"

const bool has_error_code[] = {
    false, false, false, false, false, false, false, false, // 0-7
    true,  false, true,  true,  true,  true,  true,  false, // 8-15
    false, false, false, false, false, false, false, false, // 16-23
    false, false, false, false, false, false, false, false  // 24-31
};

void isr_handler(int vec_num, REGS* r) {
    // Print exception or IRQ number for now, no interrupt handling.

    // keyboard interrupt
    if (vec_num == 33) { // 0x21
        keyboard_handler();
        return;
    }

    // timer
    if (vec_num == 32) { // 0x20
        timer_handler();
        return;
    }

    if (vec_num < 32) {
        print_to_screen("Exception: ", COLOR_RED);
        print_dec(vec_num, COLOR_WHITE);
        print_to_screen(" \r\n", COLOR_BLACK);
    }

    if (r->error_code && has_error_code[vec_num]) {
        print_to_screen("Error Code: ", COLOR_YELLOW);
        print_dec(r->error_code, COLOR_WHITE);
        print_to_screen(" \r\n", COLOR_BLACK);
    }
    return;
    /*
    else {
        print_to_screen("IRQ: ", COLOR_BLUE);
        print_dec(vec_num - 32, COLOR_WHITE);
        print_to_screen(" \r\n", COLOR_BLACK);
    }
    */
}

void init_interrupts() {
	install_idt();
}