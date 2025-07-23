/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     GOP Driver to draw onto screen Implementation.
 */

#include "gop.h"

void draw_char(GOP_PARAMS* gop, char c, uint32_t x, uint32_t y, uint32_t color) {
    // Check bounds
    if (c < 32 || c > 126) return;

    const uint8_t* bitmap = font8x8_basic[c - 32];

    for (uint8_t row = 0; row < 8; row++) {
        for (uint8_t col = 0; col < 8; col++) {
            if (bitmap[row] & (1 << (7 - col))) {
                // Make characters bigger (4x4 pixels per font pixel)
                for (int sx = 0; sx < 4; sx++) {
                    for (int sy = 0; sy < 4; sy++) {
                        if (x + col * 4 + sx < gop->Width && y + row * 4 + sy < gop->Height) {
                            plot_pixel(gop, x + col * 4 + sx, y + row * 4 + sy, color);
                        }
                    }
                }
            }
        }
    }
}

void draw_string(GOP_PARAMS* gop, const char* s, uint32_t x, uint32_t y, uint32_t color) {
    // While the ptr is valid.
    while (*s) {
        draw_char(gop, *s++, x, y, color);
        // Increment X by the scaled width of one character.
        x += FONT_WIDTH * FONT_SCALE; // Corrected from x += 8;
    }
}