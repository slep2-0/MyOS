/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     VGA I/O Functions Implementation.
 */

#include "vga.h"
// didn't know static made variables PRIVATE to the file only, so I couldn't externally link them to another object file (handlers.o), removed static.
int cursor_x = 0;
int cursor_y = 0;
int cursor_visible = 0;
unsigned char current_bg_color = COLOR_BLACK;  // default background

/* Clear the entire screen */
void clear_screen(unsigned char attribute) {
    current_bg_color = (attribute >> 4) & 0x0F;  // extract bg from attribute

    volatile unsigned short* video_memory = (volatile unsigned short*)VGA_MEMORY;
    unsigned short blank = (unsigned short)(attribute << 8) | ' ';

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        video_memory[i] = blank;
    }

    cursor_x = 0;
    cursor_y = 0;
}


unsigned char make_color(unsigned char foreground, unsigned char background) {
	return (unsigned char)((background << 4) | (foreground & 0x0F));
}

void print_to_screen(char* text, unsigned char fg_color) {
    volatile unsigned char* video_memory = (volatile unsigned char*)0xB8000;
    unsigned char attribute = (unsigned char)((current_bg_color << 4) | (fg_color & 0x0F));

    for (int i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];
        int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;

        if (c == '\r') {
            video_memory[offset] = ' ';
            video_memory[offset + 1] = attribute;
            cursor_x = 0;
        }
        else if (c == '\n') {
            cursor_x = 0;
            cursor_y++;
        }
        else if (c == '\b') {
            video_memory[offset] = ' ';
            video_memory[offset + 1] = attribute;

            if (cursor_x > 0) cursor_x--;
            else if (cursor_y > 0) {
                cursor_y--;
                cursor_x = VGA_WIDTH - 1;
            }

            int pos = (cursor_y * VGA_WIDTH + cursor_x) * 2;
            video_memory[pos] = ' ';
            video_memory[pos + 1] = attribute;

            cursor_visible = 0;
        }
        else {
            video_memory[offset] = c;
            video_memory[offset + 1] = attribute;
            cursor_x++;
        }

        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
        if (cursor_y >= VGA_HEIGHT) {
            cursor_y = 0;  // or scroll
        }
    }
}

void print_to_screen_custom_background_foreground(char* text, unsigned char attribute) {
    volatile unsigned char* video_memory = (volatile unsigned char*)0xB8000;

    for (int i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c == '\r') {
            // remove cursor when doing a carriage return.
            int old_offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
            video_memory[old_offset] = ' ';
            video_memory[old_offset + 1] = attribute;
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
            video_memory[offset] = c;
            video_memory[offset + 1] = attribute;
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
void print_dec(unsigned int num, unsigned char color) {
    unsigned char buf[12];   // enough for �4294967295\0�
    int i = 0;

    if (num == 0) {
        buf[i++] = '0';
    }
    else {
        // build digits in reverse
        while (num > 0 && (unsigned)i < sizeof(buf) - 1) {
            buf[i++] = (unsigned char)('0' + (num % 10)); // Get the least significant bit first, if we wouldn't do in reverse, 1234 would be 4321.
            num /= 10;
        }
    }
    buf[i] = '\0';

    // reverse the buffer
    for (int j = 0; j < i / 2; j++) {
        unsigned char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }

    // now print the string
    print_to_screen((char*)buf, color);
}

void print_dec_custom_background_foreground(unsigned int num, unsigned char attribute) {
    unsigned char buf[12];   // enough for �4294967295\0�
    int i = 0;

    if (num == 0) {
        buf[i++] = '0';
    }
    else {
        // build digits in reverse
        while (num > 0 && (unsigned)i < sizeof(buf) - 1) {
            buf[i++] = (unsigned char)('0' + (num % 10)); // Get the least significant bit first, if we wouldn't do in reverse, 1234 would be 4321.
            num /= 10;
        }
    }
    buf[i] = '\0';

    // reverse the buffer
    for (int j = 0; j < i / 2; j++) {
        unsigned char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }

    // now print the string
    print_to_screen_custom_background_foreground((char*)buf, attribute);
}

void set_hardware_cursor_position(int x, int y) {
    unsigned int pos = (unsigned int)(y * VGA_WIDTH + x);

    __outbyte(VGA_CTRL_REG, VGA_CURSOR_LOW);
    __outbyte(VGA_DATA_REG, (unsigned char)(pos & 0xFF));
    __outbyte(VGA_CTRL_REG, VGA_CURSOR_HIGH);
    __outbyte(VGA_DATA_REG, (unsigned char)((pos >> 8) & 0xFF));
}

int old_cursor_x = 0, old_cursor_y = 0;
char char_under_cursor = ' ';  // character under current cursor

void update_char_under_cursor(void) {
    volatile char* video_memory = (volatile char*)0xB8000;
    int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
    char_under_cursor = video_memory[offset];
}


void blink_cursor(void) {
    volatile char* video_memory = (volatile char*)0xB8000;
    int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;

    if (cursor_visible) {
        // Restore original character
        video_memory[offset] = char_under_cursor;
        video_memory[offset + 1] = COLOR_WHITE;
        cursor_visible = 0;
    }
    else {
        // Save current character before drawing _
        char_under_cursor = video_memory[offset];
        video_memory[offset] = '_';
        cursor_visible = 1;
    }
}

void myos_printf(unsigned char color, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (int i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            i++;
            char spec = fmt[i];

            if (spec == 'd') {
                int int_val = va_arg(args, int);
                print_dec((unsigned int)int_val, color);
            }
            else if (spec == 's') {
                const char* str_val = va_arg(args, const char*);
                if (str_val) print_to_screen((char*)str_val, color);
            }
            else if (spec == 'x') {
                unsigned int hex_val = va_arg(args, unsigned int);
                print_hex(hex_val, color);
            }
            else {
                // Unknown specifier, print as is
                char s[2] = { '%', '\0' };
                print_to_screen(s, color);
                char s2[2] = { spec, '\0' };
                print_to_screen(s2, color);
            }
        }
        else {
            char s[2] = { fmt[i], '\0' };
            print_to_screen(s, color);
        }
    }

    va_end(args);
}

void print_hex(unsigned int value, unsigned char color) {
    unsigned char buf[9];

    // Produce exactly 8 hex digits
    for (int i = 0; i < 8; i++) {
        // take the highest nibble first
        unsigned int shift = (unsigned int)((7 - i) * 4);
        unsigned int nibble = (value >> shift) & 0xF;

        // Convert nibble to hex character manually
        if (nibble < 10) {
            buf[i] = (unsigned char)('0' + nibble);
        }
        else {
            buf[i] = (unsigned char)('A' + (nibble - 10));
        }
    }
    buf[8] = '\0';

    print_to_screen((char*)buf, color);
    print_to_screen(" ", color);  // trailing space for readability
}

void print_hex_custom_background_foreground(unsigned int value, unsigned char attribute) {
    unsigned char buf[9];

    // Produce exactly 8 hex digits
    for (int i = 0; i < 8; i++) {
        // take the highest nibble first
        unsigned int shift = (unsigned int)((7 - i) * 4);
        unsigned int nibble = (value >> shift) & 0xF;

        // Convert nibble to hex character manually
        if (nibble < 10) {
            buf[i] = (unsigned char)('0' + nibble);
        }
        else {
            buf[i] = (unsigned char)('A' + (nibble - 10));
        }
    }
    buf[8] = '\0';

    print_to_screen_custom_background_foreground((char*)buf, attribute);
    print_to_screen_custom_background_foreground(" ", attribute);  // trailing space for readability
}