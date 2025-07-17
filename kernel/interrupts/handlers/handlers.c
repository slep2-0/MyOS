/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Implementation of handler functions for interrupts.
 */

#include "handlers.h"
#include "scancodes.h"

char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

char scancode_to_ascii_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};


bool shift_pressed = false;
bool ctrl_pressed = false;
bool caps_lock_on = false;

void init_keyboard() {
    shift_pressed = false;
    ctrl_pressed = false;
    caps_lock_on = false;
}

void keyboard_handler() {
    // Read the scan port from the status port.
    unsigned char scancode = __inbyte(KEYBOARD_DATA_PORT);
    // Check if it's a key press, bit 7 of the inbyte.
    // 0x80 in binary is 1000 0000 -> BIT 7 AND -> if 1 - press, if 0 release.
    if (!(scancode & 0x80)) {
        // Conver scan code to ASCII to see if it's even a printable character.
        if (scancode < sizeof(scancode_to_ascii) && scancode_to_ascii[scancode]) {
            char key = scancode_to_ascii[scancode];
            char keyShift = scancode_to_ascii_shift[scancode];
            char str[2] = { key, '\0' }; // default string (key) + null terminator.
            char strShift[2] = { keyShift, '\0' };
            switch (key) {
            case '\n': // newline char
                print_to_screen("\r\n", COLOR_BLACK);
                break;
            case '\b': // backspace
                /* FIXME TODO : Implement backspace handling in print_to_screen */
                print_to_screen("\b \b", COLOR_BLACK);
                break;
            case '\t': // TAB
                print_to_screen("    ", COLOR_BLACK);
                break;
            default:
                if (shift_pressed || caps_lock_on) {
                    print_to_screen(&strShift, COLOR_WHITE);
                }
                else {
                    print_to_screen(&str, COLOR_WHITE);
                }
                break;
            }
        }
        switch (scancode) {
        case KEYBOARD_SCANCODE_PRESSED_LEFT_SHIFT:
            shift_pressed = true;
            break;
        case KEYBOARD_SCANCODE_PRESSED_CAPS_LOCK:
            caps_lock_on = !caps_lock_on;
        }

    }
    else {
        // It's a release, bit 7 is 0.
        switch (scancode) {
        case KEYBOARD_SCANCODE_RELEASE_LEFT_SHIFT:
            shift_pressed = false;
            break;
        }

    }
    // Send End Of Interrupt (EOI) to the PIC.
    __outbyte(0x20, PIC_EOI); // Only sent to master since this is a master interrupt.
}

void init_timer(unsigned long int frequency) {
    unsigned long int divisor = 1193180 / frequency;

    // Send the command byte
    __outbyte(0x43, 0x36);  // Channel 0, lobyte/hibyte, mode 3 (square wave)

    // Send the frequency divisor
    __outbyte(0x40, (unsigned char)(divisor & 0xFF));       // Low byte
    __outbyte(0x40, (unsigned char)((divisor >> 8) & 0xFF)); // High byte
}

static int tick = 0;
static int cursor_last_blink = 0;

void timer_handler() {
    tick++;
    if (tick % 20 == 0) {
        blink_cursor(); // Every 20 timer interrupts
    }
}