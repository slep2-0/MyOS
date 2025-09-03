/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     Keyboard Scancodes Definitions.
 */
#ifndef X86_KEYBOARD_SCANCODES
#define X86_KEYBOARD_SCANCODES

// taken from here - https://wiki.osdev.org/PS/2_Keyboard

// press
#define KEYBOARD_SCANCODE_PRESSED_3 0x04
#define KEYBOARD_SCANCODE_PRESSED_7 0x08
#define KEYBOARD_SCANCODE_PRESSED_MINUS 0x0C
#define KEYBOARD_SCANCODE_PRESSED_Q 0x10
#define KEYBOARD_SCANCODE_PRESSED_T 0x14
#define KEYBOARD_SCANCODE_PRESSED_O 0x18
#define KEYBOARD_SCANCODE_PRESSED_ENTER 0x1C
#define KEYBOARD_SCANCODE_PRESSED_D 0x20
#define KEYBOARD_SCANCODE_PRESSED_J 0x24
#define KEYBOARD_SCANCODE_PRESSED_SINGLEQUOTE 0x28
#define KEYBOARD_SCANCODE_PRESSED_Z 0x2C
#define KEYBOARD_SCANCODE_PRESSED_B 0x30
#define KEYBOARD_SCANCODE_PRESSED_LEFT_SHIFT 0x2A
#define KEYBOARD_SCANCODE_PRESSED_DOT 0x34
#define KEYBOARD_SCANCODE_PRESSED_LEFT_ALT 0x38
#define KEYBOARD_SCANCODE_PRESSED_LEFT_CONTROL 0x1D
#define KEYBOARD_SCANCODE_PRESSED_CAPS_LOCK 0x3A

// extended press
#define KEYBOARD_SCANCODE_EXTENDED_PRESSED_CURSOR_UP 0x48
#define KEYBOARD_SCANCODE_EXTENDED_PRESSED_CURSOR_LEFT 0x4B
#define KEYBOARD_SCANCODE_EXTENDED_PRESSED_CURSOR_DOWN 0x50
#define KEYBOARD_SCANCODE_EXTENDED_PRESSED_CURSOR_RIGHT 0x4D

// release
#define KEYBOARD_SCANCODE_RELEASE_LEFT_CONTROL 0x9D
#define KEYBOARD_SCANCODE_RELEASE_LEFT_SHIFT 0xAA
#define KEYBOARD_SCANCODE_RELEASE_CAPS_LOCK 0xBB

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


// yeah im tired, this is enough for now.

#endif
