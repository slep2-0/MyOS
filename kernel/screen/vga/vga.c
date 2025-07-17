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
	/* Setup a pointer to video memory (VGA x86) */
	char* video_memory = (char*)0xB8000;
	for (int i = 0; text[i] != '\0'; i++) {
		video_memory[i * 2] = text[i]; // I * 2 because we go straight to the text buffer, since i++ will give us the next iteration of I, which would be color->text->color, now we go straight to the next 2 bytes, which are text,color.
		video_memory[i * 2 + 1] = color;
	}
}