/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     VGA I/O Functions IMPLEMENTATION.
 */

#include "vga.h"

static int cursor_x = 0;
static int cursor_y = 0;

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
            // the most left
            cursor_x = 0;
        }

        else if (c == '\n') {
            cursor_x = 0;
            // down.
            cursor_y++;
        }

        else if (c == '\b') {
            if (cursor_x > 0) {
                cursor_x--;
            }
            else if (cursor_y > 0) {
                cursor_y--;
                cursor_x = 79; // move to the end of the previous line (remember the absolute end is 80)
            }

            int position = (cursor_y * 80 + cursor_x) * 2;
            video_memory[position] = ' '; // overwrite the character
            video_memory[position + 1] = COLOR_BLACK; // clear it (black screen behind)
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

void set_cursor_position(int x, int y) {
    unsigned short pos = y * VGA_WIDTH + x;

    __outbyte(VGA_CTRL_REG, VGA_CURSOR_LOW);
    __outbyte(VGA_DATA_REG, (unsigned char)(pos & 0xFF));
    __outbyte(VGA_CTRL_REG, VGA_CURSOR_HIGH);
    __outbyte(VGA_DATA_REG, (unsigned char)((pos >> 8) & 0xFF));
}

static int cursor_visible = 0;

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
