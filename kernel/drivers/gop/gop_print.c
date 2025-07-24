// gop_print.c
#include "gop_print.h"
#include "fonttable.h"

static GOP_PARAMS* gt;
static uint32_t    tcolor;

void gop_text_init(GOP_PARAMS* gop, uint32_t color) {
    gt = gop;
    tcolor = color;
}

void gop_text(const char* s, uint32_t x, uint32_t y) {
    while (*s) {
        char c = *s++;
        if (c < 32 || c > 126) continue;
        const uint8_t* bm = font8x8_basic[c - 32];
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) if (bm[row] & (1 << (7 - col))) {
                // 4×4 block
                uint32_t px = x + col * 4;
                uint32_t py = y + row * 4;
                for (int dy = 0; dy < 4; dy++)
                    for (int dx = 0; dx < 4; dx++)
                        plot_pixel(gt, px + dx, py + dy, tcolor);
            }
        }
        x += 8 * 4;
    }
}
