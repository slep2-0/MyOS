/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     GOP Driver to draw onto screen (long‑mode framebuffer)
 */

#ifndef X86_GOP_DRIVER_H
#define X86_GOP_DRIVER_H

#include "../../kernel.h"

 // integer font scale (1 = native 8×16, 2 = 16×32, etc)
#define FONT_SCALE 1

static inline void plot_pixel(GOP_PARAMS* gop, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t* fb = (uint32_t*)(uintptr_t)gop->FrameBufferBase;
    uint32_t  stride = gop->PixelsPerScanLine;
    fb[y * stride + x] = color;
}

static inline uint32_t char_width(void) { return  8 * FONT_SCALE; }
static inline uint32_t line_height(void) { return 16 * FONT_SCALE; }

void draw_char(GOP_PARAMS* gop, char c, uint32_t x, uint32_t y, uint32_t color);
void draw_string(GOP_PARAMS* gop, const char* s, uint32_t x, uint32_t y, uint32_t color);

void gop_printf(GOP_PARAMS* gop, uint32_t color, const char* fmt, ...);
void gop_put_char(GOP_PARAMS* gop, char c, uint32_t color);
void gop_puts(GOP_PARAMS* gop, const char* s, uint32_t color);
void gop_scroll(GOP_PARAMS* gop);
void gop_clear_screen(GOP_PARAMS* gop, uint32_t color);

#endif // X86_GOP_DRIVER_H
