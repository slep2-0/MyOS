/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     GOP Driver to draw onto screen (switched from VGA since we are in long mode)
 */
#ifndef X86_GOP_DRIVER_H
#define X86_GOP_DRIVER_H

#include "../../kernel.h"
#include "fonttable.h"

#define FONT_SCALE 1.5f

static inline void plot_pixel(GOP_PARAMS* gop, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t* fb = (uint32_t*)(uintptr_t)gop->FrameBufferBase;
    uint32_t  stride = gop->PixelsPerScanLine;

    // Direct pixel mapping without inversion:
    fb[y * stride + x] = color;
}

// Draw 1 character in the screen.
void draw_char(GOP_PARAMS* gop, char c, uint32_t x, uint32_t y, uint32_t color);

// Draw an entire string in the screen.
void draw_string(GOP_PARAMS* gop, const char* s, uint32_t x, uint32_t y, uint32_t color);

// Draw a formatted string at current cursor position with color
void gop_printf(GOP_PARAMS* gop, uint32_t color, const char* fmt, ...);

// Print a single character with formatting (handles newline, wrap, etc)
void gop_put_char(GOP_PARAMS* gop, char c, uint32_t color);

// Print a null-terminated string with formatting
void gop_puts(GOP_PARAMS* gop, const char* s, uint32_t color);

// Print unsigned decimal number
void gop_print_dec(GOP_PARAMS* gop, unsigned int val, uint32_t color);

// Print hexadecimal number with 0x prefix
void gop_print_hex(GOP_PARAMS* gop, unsigned int val, uint32_t color);

// Scrolls the screen one line upward
void gop_scroll(GOP_PARAMS* gop);

//void gop_set_cursor(uint32_t x, uint32_t y);

// Optionally: Clear the whole screen (not implemented yet, but easily added)
void gop_clear_screen(GOP_PARAMS* gop, uint32_t color);

#endif