/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:     GOP Driver to draw onto screen Implementation (8×16 font)
 */

#include "gop.h"
#define FONT8X16_IMPLEMENTATION
#include "font8x16.h"

bool gop_bold_enabled = false; // default
uint32_t cursor_x = 0, cursor_y = 0;
extern GOP_PARAMS gop_local;

void draw_char(GOP_PARAMS* gop, char c_, uint32_t x, uint32_t y, uint32_t color) {
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
    while (*s) {
        draw_char(gop, *s, x, y, color);
        x += char_width();
        s++;
    }
}

void gop_scroll(GOP_PARAMS* gop) {
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
    while (*s) {
        gop_put_char(gop, *s++, color);
    }
}

static void sprint_dec(char* buf, unsigned v) {
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
    char buf[16];
    sprint_dec(buf, val);
    gop_puts(gop, buf, color);
}

void gop_print_hex(GOP_PARAMS* gop, uint64_t val, uint32_t color) {
    char buf[19] = "0x0000000000000000"; // 64 bit addressing
    for (int i = 0; i < 16; i++) {
        unsigned nib = (val >> ((15 - i) * 4)) & 0xF;
        buf[2 + i] = (nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
    buf[18] = '\0'; // null terminator
    gop_puts(gop, buf, color);
}

extern GOP_PARAMS gop_local;

void gop_clear_screen(GOP_PARAMS* gop, uint32_t color) {
    for (uint32_t y = 0; y < gop->Height; y++)
        for (uint32_t x = 0; x < gop->Width; x++)
            plot_pixel(gop, x, y, color);
}

static inline void buf_put_char(char* buf, size_t size, size_t* written, char c) {
    if (*written + 1 < size) {
        buf[*written] = c;
    }
    (*written)++;
}

static void buf_puts(char* buf, size_t size, size_t* written, const char* s) {
    while (*s) {
        buf_put_char(buf, size, written, *s++);
    }
}

static void buf_print_dec(char* buf, size_t size, size_t* written, int value) {
    char tmp[12]; // enough for -2^31 and NUL
    char* t = tmp + sizeof(tmp) - 1;
    bool neg = (value < 0);
    unsigned u = neg ? -(unsigned)value : (unsigned)value;
    *t = '\0';
    if (u == 0) {
        *--t = '0';
    }
    else {
        while (u) {
            *--t = '0' + (u % 10);
            u /= 10;
        }
    }
    if (neg) {
        *--t = '-';
    }
    buf_puts(buf, size, written, t);
}

static void buf_print_hex(char* buf, size_t size, size_t* written, unsigned value) {
    char tmp[9]; // 8 digits + NUL
    char* t = tmp + sizeof(tmp) - 1;
    const char* hex = "0123456789abcdef";
    *t = '\0';
    if (value == 0) {
        *--t = '0';
    }
    else {
        while (value) {
            *--t = hex[value & 0xF];
            value >>= 4;
        }
    }
    buf_puts(buf, size, written, t);
}

//-----------------------------------------------------------------------------
// Helper: simple strchr for delimiter scanning
//-----------------------------------------------------------------------------
static char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char*)s;
        }
        s++;
    }
    return NULL;
}

//-----------------------------------------------------------------------------
// kstrlen: Return length of string (excluding null terminator).
//-----------------------------------------------------------------------------
size_t kstrlen(const char* str) {
    size_t len = 0;
    while (str && str[len] != '\0') {
        len++;
    }
    return len;
}

//-----------------------------------------------------------------------------
// kstrcpy: Copy string from src to dst. Assumes dst is large enough.
//-----------------------------------------------------------------------------
char* kstrcpy(char* dst, const char* src) {
    char* ret = dst;
    while ((*dst++ = *src++)) {
        // copy until null terminator
    }
    return ret;
}

//-----------------------------------------------------------------------------
// kstrtok: Tokenize string with delimiters.
// Works like strtok, but without libc.
// Keeps static state across calls unless str != NULL.
//-----------------------------------------------------------------------------
char* kstrtok(char* str, const char* delim) {
    static char* saved = NULL;

    if (str != NULL) {
        saved = str;
    }
    if (saved == NULL) {
        return NULL;
    }

    // Skip leading delimiters
    char* token_start = saved;
    while (*token_start && strchr(delim, *token_start)) {
        token_start++;
    }
    if (*token_start == '\0') {
        saved = NULL;
        return NULL;
    }

    // Find end of token
    char* token_end = token_start;
    while (*token_end && !strchr(delim, *token_end)) {
        token_end++;
    }

    if (*token_end) {
        *token_end = '\0';
        saved = token_end + 1;
    }
    else {
        saved = NULL;
    }

    return token_start;
}

int ksnprintf(char* buf, size_t bufsize, const char* fmt, ...) {
    size_t written = 0;
    va_list ap;
    va_start(ap, fmt);

    for (const char* p = fmt; *p; p++) {
        if (*p == '%' && p[1]) {
            p++;
            switch (*p) {
            case 'd':
                buf_print_dec(buf, bufsize, &written, va_arg(ap, int32_t));
                break;      
            case 'u':       
                buf_print_dec(buf, bufsize, &written, va_arg(ap, uint32_t));
                break;   
            case 'x':    
                buf_print_hex(buf, bufsize, &written, va_arg(ap, uint32_t));
                break;      
            case 'p':       
                buf_print_hex(buf, bufsize, &written, (unsigned)(uintptr_t)va_arg(ap, void*));
                break;
            case 'c':
                buf_put_char(buf, bufsize, &written, (char)va_arg(ap, int));
                break;
            case 's': {
                const char* s = va_arg(ap, const char*);
                buf_puts(buf, bufsize, &written, s ? s : "(null)");
                break;
            }
            case '%':
                buf_put_char(buf, bufsize, &written, '%');
                break;
            default:
                buf_put_char(buf, bufsize, &written, '%');
                buf_put_char(buf, bufsize, &written, *p);
            }
        }
        else {
            buf_put_char(buf, bufsize, &written, *p);
        }
    }

    va_end(ap);
    if (bufsize > 0) {
        buf[written < bufsize ? written : bufsize - 1] = '\0';
    }

    return (int)written;
}

static inline bool interrupts_enabled(void) {
    unsigned long flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    return (flags & (1UL << 9)) != 0; // IF is bit 9
}

static void gop_print_binary(GOP_PARAMS* gop, uint64_t val, uint32_t color) {
    char buf[65]; // 64 bits + null terminator
    for (int i = 0; i < 64; i++) {
        // fill buffer from MSB to LSB
        buf[i] = (val & (1ULL << (63 - i))) ? '1' : '0';
    }
    buf[64] = '\0';
    gop_puts(gop, buf, color);
}

int kstrcmp(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        if (*s1 != *s2) return (int)((unsigned char)*s1 - (unsigned char)*s2);
        s1++;
        s2++;
    }
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

void gop_printf(uint32_t color, const char* fmt, ...) {
    bool prev_if = interrupts_enabled();
    __cli();
    GOP_PARAMS* gop = &gop_local;
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
            case 'd': gop_print_dec(gop, va_arg(ap, int64_t), color); break;
            case 'u': gop_print_dec(gop, va_arg(ap, uint64_t), color); break;
            case 'x': gop_print_hex(gop, va_arg(ap, uint64_t), color); break;
            case 'p': gop_print_hex(gop, (uint64_t)(uintptr_t)va_arg(ap, void*), color); break;
            case 'c': gop_put_char(gop, (char)va_arg(ap, uint64_t), color); break;
            case 'b': gop_print_binary(gop, (char)va_arg(ap, uint64_t), color); break;
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
   if (prev_if) __sti();
}
