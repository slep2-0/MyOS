#include "handlers.h"

char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

void keyboard_handler() {
    // Read the scan port from the status port.
    unsigned char scancode = __inbyte(KEYBOARD_DATA_PORT);
    // Check if it's a key press, bit 7 of the inbyte - 0 release - 1 press.
    if (!(scancode & 0x80)) {
        // Conver scan code to ASCII to see if it's even a printable character.
        if (scancode < sizeof(scancode_to_ascii) && scancode_to_ascii[scancode]) {
            char key = scancode_to_ascii[scancode];
            char str[2] = { key, '\0' }; // default string (key) + null terminator.
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
                print_to_screen(&str, COLOR_WHITE);
                break;
            }
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