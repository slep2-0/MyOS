/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     GOP Driver to draw onto screen Implementation.
 */
#include "gop.h"

// Cursor position (in pixels)
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;

void draw_char(GOP_PARAMS* gop, char c, uint32_t x, uint32_t y, uint32_t color) {
    if (c < 32 || c > 126) return;
    const uint8_t* bitmap = font8x8_basic[c - 32];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (bitmap[row] & (1 << col)) {
                for (int dy = 0; dy < (int)(FONT_SCALE + 0.5f); dy++) {
                    uint32_t py = y + (uint32_t)(row * FONT_SCALE + dy);
                    if (py >= gop->Height) continue;
                    for (int dx = 0; dx < (int)(FONT_SCALE + 0.5f); dx++) {
                        uint32_t px = x + (uint32_t)(col * FONT_SCALE + dx);
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



static inline uint32_t char_width() {
    return (uint32_t)(8 * FONT_SCALE + 0.5f);
}

static inline uint32_t line_height() {
    return (uint32_t)(8 * FONT_SCALE + 0.5f);
}

void gop_scroll(GOP_PARAMS* gop) {
    uint32_t* fb = (uint32_t*)(uintptr_t)gop->FrameBufferBase;
    uint32_t stride = gop->PixelsPerScanLine;
    uint32_t height = gop->Height;
    uint32_t width = gop->Width;

    uint32_t scroll_lines = line_height();

    // Move framebuffer contents up by one line
    for (uint32_t y = 0; y < height - scroll_lines; y++) {
        kmemcpy(
            &fb[y * stride],
            &fb[(y + scroll_lines) * stride],
            width * sizeof(uint32_t)
        );
    }

    // Clear the bottom scroll area
    for (uint32_t y = height - scroll_lines; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            fb[y * stride + x] = 0x00000000;  // Black background
        }
    }

    cursor_y -= scroll_lines;
    if ((int)cursor_y < 0) cursor_y = 0;
}

void gop_put_char(GOP_PARAMS* gop, char c, uint32_t color) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += line_height();
        if (cursor_y + line_height() > gop->Height) gop_scroll(gop);
        return;
    }
    else if (c == '\r') {
        cursor_x = 0;
        return;
    }

    draw_char(gop, c, cursor_x, cursor_y, color);
    cursor_x += char_width();

    // Wrap line
    if (cursor_x + char_width() > gop->Width) {
        cursor_x = 0;
        cursor_y += line_height();
        if (cursor_y + line_height() > gop->Height) gop_scroll(gop);
    }
}

void gop_puts(GOP_PARAMS* gop, const char* s, uint32_t color) {
    while (*s) {
        gop_put_char(gop, *s++, color);
    }
}

void gop_print_dec(GOP_PARAMS* gop, unsigned int val, uint32_t color) {
    char buf[16];
    int i = 0;
    if (val == 0) buf[i++] = '0';
    else {
        while (val > 0) {
            buf[i++] = '0' + (val % 10);
            val /= 10;
        }
    }
    buf[i] = '\0';

    // Reverse
    for (int j = 0; j < i / 2; ++j) {
        char tmp = buf[j];
        buf[j] = buf[i - j - 1];
        buf[i - j - 1] = tmp;
    }

    gop_puts(gop, buf, color);
}

void gop_print_hex(GOP_PARAMS* gop, unsigned int val, uint32_t color) {
    char buf[11] = "0x00000000";
    for (int i = 0; i < 8; i++) {
        uint8_t nibble = (val >> ((7 - i) * 4)) & 0xF;
        buf[2 + i] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
    }
    gop_puts(gop, buf, color);
}

void gop_printf(GOP_PARAMS* gop, uint32_t color, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (int i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] == '%' && fmt[i + 1] != '\0') {
            i++;
            char spec = fmt[i];

            switch (spec) {
            case 'd': {
                int val = va_arg(args, int);
                gop_print_dec(gop, (unsigned int)val, color);
                break;
            }
            case 'u': {
                unsigned int val = va_arg(args, unsigned int);
                gop_print_dec(gop, val, color);
                break;
            }
            case 'x': {
                unsigned int val = va_arg(args, unsigned int);
                gop_print_hex(gop, val, color);
                break;
            }
            case 'p': {
                void* ptr = va_arg(args, void*);
                gop_print_hex(gop, (unsigned int)(uintptr_t)ptr, color);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                gop_put_char(gop, c, color);
                break;
            }
            case 's': {
                const char* str = va_arg(args, const char*);
                if (str) gop_puts(gop, str, color);
                break;
            }
            case '%': {
                gop_put_char(gop, '%', color);
                break;
            }
            default: {
                gop_put_char(gop, '%', color);
                gop_put_char(gop, spec, color);
                break;
            }
            }
        }
        else {
            gop_put_char(gop, fmt[i], color);
        }
    }

    va_end(args);
}

void gop_clear_screen(GOP_PARAMS* gop, uint32_t color) {
    // Clear screen via GOP
    for (uint32_t y = 0; y < gop->Height; y++)
        for (uint32_t x = 0; x < gop->Width; x++)
            plot_pixel(gop, x, y, color);
}