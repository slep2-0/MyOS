/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     GOP Driver to draw onto screen Implementation.
 */
#include "gop.h"

void draw_char(GOP_PARAMS* gop, char c, uint32_t x, uint32_t y, uint32_t color) {
    if (c < 32 || c > 126) return;
    const uint8_t* bitmap = font8x8_basic[c - 32];

    // For each bit in the 8×8 glyph:
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (bitmap[row] & (1 << (7 - col))) {
                // Paint an FONT_SCALE×FONT_SCALE block
                for (int dy = 0; dy < FONT_SCALE; dy++) {
                    uint32_t py = y + row * FONT_SCALE + dy;
                    if (py >= gop->Height) continue;
                    for (int dx = 0; dx < FONT_SCALE; dx++) {
                        uint32_t px = x + col * FONT_SCALE + dx;
                        if (px < gop->Width) {
                            plot_pixel(gop, px, py, color);
                        }
                    }
                }
            }
        }
    }
}

void draw_string(GOP_PARAMS* gop, const char* s, uint32_t x, uint32_t y, uint32_t color) {
    // For each character advance by exactly 8*FONT_SCALE pixels
    while (*s) {
        draw_char(gop, *s++, x, y, color);
        x += 8 * FONT_SCALE;
        // optional: wrap or newline handling here
    }
}
