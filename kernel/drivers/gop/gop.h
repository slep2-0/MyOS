/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     GOP Driver to draw onto screen (switched from VGA since we are in long mode)
 */
#ifndef X86_GOP_DRIVER_H
#define X86_GOP_DRIVER_H

#include "../../kernel.h"
#include "fonttable.h"

#define FONT_SCALE 1
#define FONT_WIDTH 4

static inline void plot_pixel(GOP_PARAMS* gop, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t* fb = (uint32_t*)(uintptr_t)gop->FrameBufferBase;
    uint32_t  stride = gop->PixelsPerScanLine;

    // invert Y so 0 is at the top
    uint32_t yf = gop->Height - 1 - y;
    // invert X so 0 is at the left
    uint32_t xf = gop->Width - 1 - x;

    fb[yf * stride + xf] = color;
}

// Draw 1 character in the screen.
void draw_char(GOP_PARAMS* gop, char c, uint32_t x, uint32_t y, uint32_t color);

// Draw an entire string in the screen.
void draw_string(GOP_PARAMS* gop, const char* s, uint32_t x, uint32_t y, uint32_t color);

#endif