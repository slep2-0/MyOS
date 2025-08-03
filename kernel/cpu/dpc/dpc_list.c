/*
 * PROJECT:      MatanelOS Kernel
 * LICENSE:      GPLv3
 * PURPOSE:		 Full DPC Function list (for kernel ISR's)
 */

#include "../cpu.h"
#include "../../interrupts/handlers/scancodes.h"

extern GOP_PARAMS gop_local;
extern uint32_t cursor_x;
extern uint32_t cursor_y;
static bool extended_scancode = false;

bool shift_pressed = false;
bool ctrl_pressed = false;
bool caps_lock_on = false;

void keyboard_dpc(void* ctxfr) {
    // Extended scancode recognition.
    CTX_FRAME* ctx = (CTX_FRAME*)ctxfr;
    bool extended = ctx->rbx;
    uint8_t scancode = ctx->rax;

    if (extended) {
        extended_scancode = true;
    }

    if (extended_scancode) {
        // second byte of extended scancode.
        switch (scancode) {
        case KEYBOARD_SCANCODE_EXTENDED_PRESSED_CURSOR_UP:
            cursor_y--; // reveresd, even though it's - on the y scale, it goes up on screen.
            extended_scancode = false;
            return;
        case KEYBOARD_SCANCODE_EXTENDED_PRESSED_CURSOR_DOWN:
            cursor_y++;
            extended_scancode = false;
            return;
        case KEYBOARD_SCANCODE_EXTENDED_PRESSED_CURSOR_RIGHT:
            cursor_x++;
            extended_scancode = false;
            return;
        case KEYBOARD_SCANCODE_EXTENDED_PRESSED_CURSOR_LEFT:
            cursor_x--;
            extended_scancode = false;
            return;
        }
    }

    // Check if it's a key press, bit 7 of the inbyte.
    // 0x80 in binary is 1000 0000 -> BIT 7 AND -> if 1 - press, if 0 release.
    if (!(scancode & 0x80)) {
        // Conver scan code to ASCII to see if it's even a printable character.
        if ((scancode < sizeof(scancode_to_ascii) && scancode_to_ascii[scancode]) || (scancode < sizeof(scancode_to_ascii_shift) && scancode_to_ascii_shift[scancode])) {
            char key = scancode_to_ascii[scancode];
            char keyShift = scancode_to_ascii_shift[scancode];
            char str[2] = { key, '\0' }; // default string (key) + null terminator.
            char strShift[2] = { keyShift, '\0' };
            switch (key) {
            case '\n': // newline char
                gop_printf(0, "\n");
                break;
            case '\b': // backspace
                gop_printf(0, "\b \b");
                break;
            case '\t': // TAB
                gop_printf(0, "    ");
                break;
            default:
                if (shift_pressed || caps_lock_on) {
                    gop_printf((uint32_t)-1, strShift); // basically underflow to max 32bit val.
                }
                else {
                    gop_printf((uint32_t)-1, str); // basically underflow to max 32bit val.
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
}

extern volatile bool schedule_pending;

void ScheduleDPC(void) {
    schedule_pending = true;
}