/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     VGA I/O Functions.
 */
#ifndef VGA
#include "../../kernel.h"
#define VGA
/* VGA Text Mode Constants */
#define VGA_MEMORY 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

/* Color definitions */
#define COLOR_BLACK 0x0
#define COLOR_BLUE 0x1
#define COLOR_GREEN 0x2
#define COLOR_CYAN 0x3
#define COLOR_RED 0x4
#define COLOR_MAGENTA 0x5
#define COLOR_BROWN 0x6
#define COLOR_LIGHT_GRAY 0x7
#define COLOR_DARK_GRAY 0x8
#define COLOR_LIGHT_BLUE 0x9
#define COLOR_LIGHT_GREEN 0xA
#define COLOR_LIGHT_CYAN 0xB
#define COLOR_LIGHT_RED 0xC
#define COLOR_LIGHT_MAGENTA 0xD
#define COLOR_YELLOW 0xE
#define COLOR_WHITE 0xF

/* PORT (PIC) Definitions */
#define VGA_CTRL_REG 0x3D4
#define VGA_DATA_REG 0x3D5
#define VGA_CURSOR_LOW  0x0F
#define VGA_CURSOR_HIGH 0x0E

// Clear the screen with the specified color.
void clear_screen(unsigned char color);

// Will return a VGA color byte value.
int make_color(int foreground, int background);

// Prints to the screen with the specified text and color
// Usage: color -> make_color(foreground, background); use color definitions. COLOR_XXXX
void print_to_screen(char* text, int color);

// Convert an unsigned decimal int into an ASCII Character number, and print to screen.
void print_dec(unsigned int num, int color);

// Set the Current CURSOR position "_"
//void set_cursor_position(int x, int y);
void blink_cursor();
#endif