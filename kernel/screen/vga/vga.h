/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     VGA I/O Functions Header.
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
// Use make color to specify background and foreground for characters over the screen, and the screen itself.
void clear_screen(unsigned char attribute);

// Will return a VGA color byte value.
int make_color(int foreground, int background);

// Prints to the screen with the specified text and color -- uses the foreground color from the clear_screen as default.
void print_to_screen(char* text, int color);

// Prints to the screen with the specified text and custom background&foreground
// Usage: attribute -> make_color(foreground, background); use color definitions. COLOR_XXXX
void print_to_screen_custom_background_foreground(char* text, int attribute);

// Convert an unsigned decimal int into an ASCII Character number, and print to screen.
void print_dec(unsigned int num, int color);

// Same thing as custom print_screen_custom_background_foreground
// Usage: attribute -> make_color(foreground, background); use color definitions. COLOR_XXXX
void print_dec_custom_background_foreground(unsigned int num, int attribute);

// Set the Current CURSOR position "_"
void set_hardware_cursor_position(int x, int y);
void blink_cursor();

// Print HEX digits to screen, lets say print_hex(0x10000, COLOR_WHITE) -> "00010000" (converts to 32bit address.)
void print_hex(unsigned int value, int color);

// Same thing as custom print_screen_custom_background_foreground
// Usage: attribute -> make_color(foreground, background); use color definitions. COLOR_XXXX
void print_hex_custom_background_foreground(unsigned int value, int attribute);

// Custom made printf -- supports %x for pointers (hex 0001000), decimal %d (%d -> 1234) and standard chars.
//void myos_printf_safe(int color, const char* fmt, ...);
void myos_printf(int color, const char* fmt, ...);
#endif