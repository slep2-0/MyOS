/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     VGA I/O Functions IMPLEMENTATION.
 */

#include "vga.h"

static int cursor_x = 0;
static int cursor_y = 0;
static int cursor_visible = 0;

/* Clear the entire screen */
void clear_screen(unsigned char color) {
	volatile unsigned short* video_memory = (volatile unsigned short*)VGA_MEMORY;
	unsigned short blank = (color << 8) | ' ';

	for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
		video_memory[i] = blank;
	}

	cursor_x = 0;
	cursor_y = 0;
}

int make_color(int foreground, int background) {
	return (background << 4) | (foreground & 0x0F);
}

void print_to_screen(char* text, int color) {
    volatile char* video_memory = (volatile char*)0xB8000;

    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];

        if (c == '\r') {
            // remove cursor when doing a carriage return.
            int old_offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
            video_memory[old_offset] = ' ';
            video_memory[old_offset + 1] = COLOR_WHITE;
            // the most left
            cursor_x = 0;
        }

        else if (c == '\n') {
            cursor_x = 0;
            // down.
            cursor_y++;
        }

        else if (c == '\b') {
            int old_offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
            video_memory[old_offset] = ' ';
            video_memory[old_offset + 1] = COLOR_WHITE;
            // it didn't delete the cursor before since it went back first so it essentially "skipped" over that video memory of the cursor.
            if (cursor_x > 0) {
                cursor_x--;
            }
            else if (cursor_y > 0) {
                cursor_y--;
                cursor_x = 79;
            }

            int position = (cursor_y * VGA_WIDTH + cursor_x) * 2;
            video_memory[position] = ' ';             
            video_memory[position + 1] = COLOR_WHITE; 

            cursor_visible = 0;
        }

        else {
            int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
            //set_cursor_position(cursor_x + 1, cursor_y);
            video_memory[offset] = c;
            video_memory[offset + 1] = color;
            cursor_x++;
        }

        // Handle line wrap
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }

        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = 0; // or scroll screen up
        }
    }
}

// Convert an unsigned int to a decimal string and print it.
void print_dec(unsigned int num, int color) {
    char buf[12];   // enough for “4294967295\0”
    int i = 0;

    if (num == 0) {
        buf[i++] = '0';
    }
    else {
        // build digits in reverse
        while (num > 0 && (unsigned)i < sizeof(buf) - 1) {
            buf[i++] = '0' + (num % 10); // Get the least significant bit first, if we wouldn't do in reverse, 1234 would be 4321.
            num /= 10;
        }
    }
    buf[i] = '\0';

    // reverse the buffer
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }

    // now print the string
    print_to_screen(buf, color);
}

void set_hardware_cursor_position(int x, int y) {
    unsigned short pos = y * VGA_WIDTH + x;

    __outbyte(VGA_CTRL_REG, VGA_CURSOR_LOW);
    __outbyte(VGA_DATA_REG, (unsigned char)(pos & 0xFF));
    __outbyte(VGA_CTRL_REG, VGA_CURSOR_HIGH);
    __outbyte(VGA_DATA_REG, (unsigned char)((pos >> 8) & 0xFF));
}

void blink_cursor() {
    volatile char* video_memory = (volatile char*)0xB8000;
    int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;

    if (cursor_visible) {
        video_memory[offset] = ' ';  // Clear
        video_memory[offset + 1] = COLOR_WHITE;
        cursor_visible = 0;
    }
    else {
        video_memory[offset] = '_';  // Draw
        video_memory[offset + 1] = COLOR_WHITE;
        cursor_visible = 1;
    }
}

void myos_printf(const char* fmt, int color, ...) {
    va_list args;
    va_start(args, color);
    // loop through all text until we hit a null terminator.
    for (int i = 0; fmt[i] != '\0'; i++) {
        // when we find the format specifider
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            // increment I to to get next char.
            i++;
            char spec = fmt[i];
            // check for which specifier it is.
            if (spec == 'd') {
                int val = va_arg(args, int);
                print_dec((unsigned int)val, color);
            }
            else if (spec == 'x') {
                unsigned int val = va_arg(args, unsigned int);
                print_hex(val, color);
            }
            else if (spec == 's') {
                char* str = va_arg(args, char*);
                print_to_screen(str, color);
            }
            else if (spec == 'c') {
                char ch = (char)va_arg(args, int);
                char s[2] = { ch, '\0' };
                print_to_screen(s, color);
            }
            else {
                // unknown specifier, js print it.
                print_to_screen("%", color);
                char s[2] = { spec, '\0' };
                print_to_screen(s, color);
            }
        }
        else {
            // no specifiers, just print it and add a null terminator at the end.
            char s[2] = { fmt[i], '\0' };
            print_to_screen(s, color);
        }
    }

    va_end(args);
}

void print_hex(unsigned int value, int color) {
    char buf[9];
    const char* hex_digits = "0123456789ABCDEF";

    // Produce exactly 8 hex digits
    for (int i = 0; i < 8; i++) {
        // take the highest nibble first
        unsigned int shift = (7 - i) * 4;
        unsigned int nibble = (value >> shift) & 0xF;
        buf[i] = hex_digits[nibble];
    }
    buf[8] = '\0';

    print_to_screen(buf, color);
    print_to_screen(" ", color);  // trailing space for readability
}