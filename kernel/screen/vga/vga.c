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
        else {
            int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
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