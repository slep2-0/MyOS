/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     GOP Driver to draw onto screen Implementation (8×16 font)
 */

#include "gop.h"
#define FONT8X16_IMPLEMENTATION
#include "font8x16.h"

bool gop_bold_enabled = false; // default
uint32_t cursor_x = 0, cursor_y = 0;
extern GOP_PARAMS gop_local;


void draw_char(GOP_PARAMS* gop, char c_, uint32_t x, uint32_t y, uint32_t color) {
    tracelast_func("draw_char");
    uint8_t c = (uint8_t)c_;
    if (c > 0x7F) return;

    const uint8_t* bitmap = font8x16[c];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = bitmap[row];
        for (int col = 0; col < 8; col++) {
            // PSF bitmaps are MSB-first
            if (!(bits & (1 << (7 - col))))
                continue;

            // scale each pixel up to FONT_SCALE×FONT_SCALE
            for (int dy = 0; dy < FONT_SCALE; dy++) {
                uint32_t py = y + row * FONT_SCALE + dy;
                if (py >= gop->Height) continue;
                for (int dx = 0; dx < FONT_SCALE; dx++) {
                    uint32_t px = x + col * FONT_SCALE + dx;
                    if (px < gop->Width) {
                        if (gop_bold_enabled) {
                            plot_pixel(gop, px, py, color);
                            plot_pixel(gop, px + 1, py, color);
                            plot_pixel(gop, px, py + 1, color);
                            plot_pixel(gop, px + 1, py + 1, color);
                        }
                        else {
                            plot_pixel(gop, px, py, color);
                        }
                    }
                }
            }
        }
    }
}

void draw_string(GOP_PARAMS* gop, const char* s, uint32_t x, uint32_t y, uint32_t color) {
    tracelast_func("draw_string");
    while (*s) {
        draw_char(gop, *s, x, y, color);
        x += char_width();
        s++;
    }
}

void gop_scroll(GOP_PARAMS* gop) {
    tracelast_func("gop_scroll");
    uint32_t* fb = (uint32_t*)(uintptr_t)gop->FrameBufferBase;
    uint32_t  stride = gop->PixelsPerScanLine;
    uint32_t  h = gop->Height;
    uint32_t  w = gop->Width;
    uint32_t  lines = line_height();

    // scroll up
    kmemcpy(&fb[0],
        &fb[lines * stride],
        (h - lines) * stride * sizeof * fb);

    // clear bottom
    for (uint32_t yy = h - lines; yy < h; yy++)
        for (uint32_t xx = 0; xx < w; xx++)
            fb[yy * stride + xx] = 0;

    cursor_y = (cursor_y >= lines) ? (cursor_y - lines) : 0;
}

void gop_put_char(GOP_PARAMS* gop, char c, uint32_t color) {
    tracelast_func("gop_put_char");
    if (c == '\b') {
        // move cursor back one character (and clear it)
        if (cursor_x >= char_width()) {
            cursor_x -= char_width();
        }
        else {
            // if at start of line, wrap to end of previous line
            if (cursor_y >= line_height()) {
                cursor_y -= line_height();
                cursor_x = gop->Width - char_width();
            }
        }
        // clear the old glyph cell:
        for (uint32_t yy = cursor_y; yy < cursor_y + line_height(); yy++) {
            for (uint32_t xx = cursor_x; xx < cursor_x + char_width(); xx++) {
                plot_pixel(gop, xx, yy, color);
            }
        }
        return;
    }
    if (c == '\n') {
        cursor_x = 0;
        cursor_y += line_height();
        if (cursor_y + line_height() > gop->Height) gop_scroll(gop);
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        return;
    }

    draw_char(gop, c, cursor_x, cursor_y, color);
    cursor_x += char_width();
    if (cursor_x + char_width() > gop->Width) {
        cursor_x = 0;
        cursor_y += line_height();
        if (cursor_y + line_height() > gop->Height) gop_scroll(gop);
    }
}

void gop_puts(GOP_PARAMS* gop, const char* s, uint32_t color) {
    tracelast_func("gop_puts");
    while (*s) {
        gop_put_char(gop, *s++, color);
    }
}

static void sprint_dec(char* buf, unsigned v) {
    tracelast_func("sprint_dec");
    char* p = buf;
    if (v == 0) { *p++ = '0'; }
    else {
        char tmp[16]; int i = 0;
        while (v) {
            tmp[i++] = '0' + (v % 10);
            v /= 10;
        }
        while (i--) *p++ = tmp[i];
    }
    *p = '\0';
}

void gop_print_dec(GOP_PARAMS* gop, unsigned val, uint32_t color) {
    tracelast_func("gop_print_dec");
    char buf[16];
    sprint_dec(buf, val);
    gop_puts(gop, buf, color);
}

void gop_print_hex(GOP_PARAMS* gop, uint64_t val, uint32_t color) {
    tracelast_func("gop_print_hex");
    char buf[19] = "0x0000000000000000"; // 64 bit addressing
    for (int i = 0; i < 16; i++) {
        unsigned nib = (val >> ((15 - i) * 4)) & 0xF;
        buf[2 + i] = (nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
    buf[18] = '\0'; // null terminator
    gop_puts(gop, buf, color);
}

void gop_printf(GOP_PARAMS* gop, uint32_t color, const char* fmt, ...) {
    tracelast_func("gop_printf");
    va_list ap;
    va_start(ap, fmt);
    for (const char* p = fmt; *p; p++) {
        if (*p == '*' && p[1] == '*') {
            gop_bold_enabled = !gop_bold_enabled;  // Toggle bold
            p++; // skip the second '*'
            continue;
        }
        if (*p == '%' && p[1]) {
            switch (*++p) {
            case 'd': gop_print_dec(gop, va_arg(ap, int), color); break;
            case 'u': gop_print_dec(gop, va_arg(ap, unsigned), color); break;
            case 'x': gop_print_hex(gop, va_arg(ap, unsigned), color); break;
            case 'p': gop_print_hex(gop, (unsigned)(uintptr_t)va_arg(ap, void*), color); break;
            case 'c': gop_put_char(gop, (char)va_arg(ap, int), color); break;
            case 's': {
                const char* str = va_arg(ap, const char*);
                if (str) gop_puts(gop, str, color);
            } break;
            case '%': gop_put_char(gop, '%', color); break;
            default:
                gop_put_char(gop, '%', color);
                gop_put_char(gop, *p, color);
            }
        }
        else {
            gop_put_char(gop, *p, color);
        }
    }
    va_end(ap);
}

void gop_clear_screen(GOP_PARAMS* gop, uint32_t color) {
    tracelast_func("gop_clear_screen");
    for (uint32_t y = 0; y < gop->Height; y++)
        for (uint32_t x = 0; x < gop->Width; x++)
            plot_pixel(gop, x, y, color);
}