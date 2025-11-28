/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     GPLv3
 * PURPOSE:     GOP Driver to draw onto screen Implementation (8×16 font)
 */

#include "gop.h"
#define FONT8X16_IMPLEMENTATION
#include "font8x16.h"
#include "../../intrinsics/atomic.h"
#include "../../includes/me.h"
#include "../../includes/macros.h"

 // integer font scale (1 = native 8×16, 2 = 16×32, etc)
#define FONT_SCALE 1

volatile void* ExclusiveOwnerShip = NULL;

static inline bool gop_params_valid(const GOP_PARAMS* gop) {
    if (!gop) return false;
    if (!gop->FrameBufferBase) return false;
    if (gop->Width == 0 || gop->Height == 0) return false;
    if (gop->PixelsPerScanLine == 0) return false;
    return true;
}

static inline void plot_pixel(GOP_PARAMS* gop, uint32_t x, uint32_t y, uint32_t color) {
    if (!gop_params_valid(gop)) return;
    if (x >= gop->Width || y >= gop->Height) return; // safeguard
    uint32_t* fb = (uint32_t*)(uintptr_t)gop->FrameBufferBase;
    uint32_t  stride = gop->PixelsPerScanLine;
    fb[y * stride + x] = color;
}

static inline uint32_t char_width(void) { return  8 * FONT_SCALE; }
static inline uint32_t line_height(void) { return 16 * FONT_SCALE; }

bool gop_bold_enabled = false; // default
uint32_t cursor_x = 0, cursor_y = 0;
extern GOP_PARAMS gop_local;

static void draw_char(GOP_PARAMS* gop, char c_, uint32_t x, uint32_t y, uint32_t color) {
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

static void draw_string(GOP_PARAMS* gop, const char* s, uint32_t x, uint32_t y, uint32_t color) {
    while (*s) {
        draw_char(gop, *s, x, y, color);
        x += char_width();
        s++;
    }
}

static void fb_memmove32(uint32_t* dest, uint32_t* src, size_t count) {
    if (dest < src) {
        // forward copy
        for (size_t i = 0; i < count; i++) dest[i] = src[i];
    }
    else if (dest > src) {
        // backward copy
        for (size_t i = count; i-- > 0; ) dest[i] = src[i];
    }
}

static void gop_scroll(GOP_PARAMS* gop) {
    uint32_t* fb = (uint32_t*)(uintptr_t)gop->FrameBufferBase;
    uint32_t  stride = gop->PixelsPerScanLine;
    uint32_t  h = gop->Height;
    uint32_t  w = gop->Width;
    uint32_t  lines = line_height();

    // scroll up - removed kmemcpy as the gop is also used in the bugcheck, and kmemcpy has Max IRQL of DISPATCH_LEVEL, while a bugcheck is HIGH_LEVEL.
    size_t count = (h - lines) * (size_t)stride;
    fb_memmove32(&fb[0], &fb[lines * stride], count);

    // clear bottom
    for (uint32_t yy = h - lines; yy < h; yy++)
        for (uint32_t xx = 0; xx < w; xx++)
            fb[yy * stride + xx] = 0;

    cursor_y = (cursor_y >= lines) ? (cursor_y - lines) : 0;
}

static void gop_put_char(GOP_PARAMS* gop, char c, uint32_t color) {
    if (!gop_params_valid(gop)) return; // defensive
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

static void gop_puts(GOP_PARAMS* gop, const char* s, uint32_t color) {
    while (*s) {
        gop_put_char(gop, *s++, color);
    }
}

static void sprint_dec(char* buf, int64_t v) {
    char* p = buf;

    if (v == 0) {
        *p++ = '0';
        *p = 0;
        return;
    }

    bool neg = false;
    if (v < 0) {
        neg = true;
        v = -v;
    }

    char tmp[20];
    int i = 0;

    while (v > 0) {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }

    if (neg) *p++ = '-';

    while (i--) {
        *p++ = tmp[i];
    }

    *p = 0;
}


static void gop_print_dec(GOP_PARAMS* gop, int64_t val, uint32_t color) {
    char buf[20];
    sprint_dec(buf, val);
    gop_puts(gop, buf, color);
}

static void gop_print_hex(GOP_PARAMS* gop, uint64_t val, uint32_t color) {
    char buf[19] = "0x0000000000000000"; // 64 bit addressing
    for (int i = 0; i < 16; i++) {
        unsigned nib = (val >> ((15 - i) * 4)) & 0xF;
        buf[2 + i] = (nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
    buf[18] = '\0'; // null terminator
    gop_puts(gop, buf, color);
}

static void gop_print_hex_minimal(GOP_PARAMS* gop, uint64_t val, uint32_t color) {
    if (val == 0) {
        gop_puts(gop, "0x0", color);
        return;
    }

    char buf[19]; // "0x" + up to 16 hex digits + null
    buf[0] = '0';
    buf[1] = 'x';

    int pos = 2;
    bool started = false;

    for (int i = 0; i < 16; i++) {
        unsigned nib = (val >> ((15 - i) * 4)) & 0xF;
        if (nib != 0 || started) {
            started = true;
            buf[pos++] = (nib < 10 ? '0' + nib : 'a' + nib - 10);
        }
    }

    buf[pos] = '\0';
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

static void buf_print_dec64(char* buf, size_t size, size_t* written, int64_t value) {
    char tmp[32]; // enough for -2^63 and NUL
    char* t = tmp + sizeof(tmp) - 1;
    bool neg = (value < 0);
    uint64_t u;
    *t = '\0';

    if (!neg) {
        u = (uint64_t)value;
    }
    else {
        // compute absolute value safely (avoid UB on INT64_MIN)
        u = (uint64_t)(-(value + 1)) + 1;
    }

    if (u == 0) {
        *--t = '0';
    }
    else {
        while (u) {
            *--t = '0' + (u % 10);
            u /= 10;
        }
    }
    if (neg) *--t = '-';
    buf_puts(buf, size, written, t);
}

static void buf_print_udec64(char* buf, size_t size, size_t* written, uint64_t value) {
    char tmp[32];
    char* t = tmp + sizeof(tmp) - 1;
    *t = '\0';
    if (value == 0) {
        *--t = '0';
    }
    else {
        while (value) {
            *--t = '0' + (value % 10);
            value /= 10;
        }
    }
    buf_puts(buf, size, written, t);
}

static void buf_print_hex64(char* buf, size_t size, size_t* written, uint64_t value) {
    char tmp[17];
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

static void buf_print_binary64(char* buf, size_t size, size_t* written, uint64_t value) {
    char tmp[65];
    char* t = tmp + sizeof(tmp) - 1;
    *t = '\0';
    if (value == 0) {
        *--t = '0';
    }
    else {
        while (value) {
            *--t = (value & 1) ? '1' : '0';
            value >>= 1;
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

/// <summary>
/// Concatenates src onto dest, up to max_len total bytes in dest.
/// </summary>
/// <param name="dest">Destination buffer (must be mutable)</param>
/// <param name="src">String to append</param>
/// <param name="max_len">Total size of the destination buffer</param>
/// <returns>Pointer to dest</returns>
char* kstrncat(char* dest, const char* src, size_t max_len) {
    if (!dest || !src || max_len == 0) return dest;

    // Move dest_ptr to the end of the current string
    size_t dest_len = 0;
    while (dest_len < max_len && dest[dest_len] != '\0') {
        dest_len++;
    }

    if (dest_len == max_len) {
        // dest is already full, cannot append
        return dest;
    }

    size_t i = 0;
    while (dest_len + i < max_len - 1 && src[i] != '\0') {
        dest[dest_len + i] = src[i];
        i++;
    }

    // Null-terminate
    dest[dest_len + i] = '\0';
    return dest;
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
// kstrncpy: Copy up to n characters from src to dst.
//           Assumes dst is large enough.
//-----------------------------------------------------------------------------
char* kstrncpy(char* dst, const char* src, size_t n) {
    if (n == 0) return dst;
    size_t i = 0;
    while (i + 1 < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return dst;
}

static inline size_t kstrlcpy(char* dst, const char* src, size_t dst_size)
{
    const char* s = src;
    size_t n = dst_size;

    if (n != 0) {
        while (--n != 0) {
            char c = *s++;
            *dst++ = c;
            if (c == '\0') {
                return (size_t)(s - src - 1);
            }
        }
        /* out of space; NUL-terminate if possible */
        *dst = '\0';
    }

    /* continue walking src to compute its length */
    while (*s++)
        ;
    return (size_t)(s - src - 1);
}

/* -------------------
 * kstrspn - like strspn
 * -------------------
 * Returns length of the initial segment of s consisting only of characters in accept.
 */
static inline size_t kstrspn(const char* s, const char* accept)
{
    const char* p = s;
    for (; *p != '\0'; ++p) {
        const char* a;
        for (a = accept; *a != '\0' && *a != *p; ++a)
            ;
        if (*a == '\0') /* char p is NOT in accept */
            break;
    }
    return (size_t)(p - s);
}

/* --------------------
 * kstrcspn - like strcspn
 * --------------------
 * Returns length of the initial segment of s consisting of characters NOT in reject.
 */
static inline size_t kstrcspn(const char* s, const char* reject)
{
    const char* p = s;
    for (; *p != '\0'; ++p) {
        const char* r;
        for (r = reject; *r != '\0' && *r != *p; ++r)
            ;
        if (*r != '\0') /* p matched a reject char */
            break;
    }
    return (size_t)(p - s);
}

//-----------------------------------------------------------------------------
// kstrtok: Tokenize string with delimiters.
// Works like strtok, but without libc.
// Keeps static state across calls unless str != NULL.
//-----------------------------------------------------------------------------
char* kstrtok_r(char* str, const char* delim, char** save_ptr)
{
    char* token_start;

    if (!save_ptr) return NULL; /* defensive */

    if (str != NULL) {
        token_start = str;
    }
    else if (*save_ptr != NULL) {
        token_start = *save_ptr;
    }
    else {
        return NULL;
    }

    /* skip leading delimiters */
    token_start += kstrspn(token_start, delim);

    if (*token_start == '\0') {
        *save_ptr = NULL;
        return NULL;
    }

    char* token_end = token_start + kstrcspn(token_start, delim);

    if (*token_end == '\0') {
        *save_ptr = NULL;
    }
    else {
        *token_end = '\0';
        *save_ptr = token_end + 1;
    }

    return token_start;
}

static void buf_print_hex64_minimal(char* buf, size_t size, size_t* written, uint64_t value) {
    char tmp[17];
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
    buf_puts(buf, size, written, "0x");
    buf_puts(buf, size, written, t);
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
                buf_print_dec64(buf, bufsize, &written, va_arg(ap, int64_t));
                break;
            case 'u':
                buf_print_udec64(buf, bufsize, &written, va_arg(ap, uint64_t));
                break;
            case 'x':
                buf_print_hex64_minimal(buf, bufsize, &written, va_arg(ap, uint64_t));
                break;
            case 'p':
                buf_puts(buf, bufsize, &written, "0x");
                buf_print_hex64(buf, bufsize, &written, (uint64_t)(uintptr_t)va_arg(ap, void*));
                break;
            case 'c':
                buf_put_char(buf, bufsize, &written, (char)va_arg(ap, int)); /* chars promoted to int */
                break;
            case 'b':
                buf_print_binary64(buf, bufsize, &written, va_arg(ap, uint64_t));
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

int kstrncmp(const char* s1, const char* s2, size_t length) {
    if (!length) return length;
    for (size_t i = 0; i < length; i++, s1++, s2++) {
        if (*s1 != *s2) return (int)((unsigned char)*s1 - (unsigned char)*s2);
        if (*s1 == '\0') return 0;
    }
    return 0;
}

SPINLOCK gop_lock = { 0 };

static void acquire_tmp_lock(SPINLOCK* lock) {
    if (!lock) return;
    // spin until we grab the lock.
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        __asm__ volatile("pause" ::: "memory"); /* x86 pause — CPU relax hint */
    }
    // Memory barrier to prevent instruction reordering
    __asm__ volatile("" ::: "memory");
}

static void release_tmp_lock(SPINLOCK* lock) {
    if (!lock) return;
    // Memory barrier before release
    __asm__ volatile("" ::: "memory");
    __sync_lock_release(&lock->locked);
}

void gop_printf(uint32_t color, const char* fmt, ...) {
    // Check for exclusive ownership, if there is none, continue, if we are the owner, continue, if we are not the owner, return.
    // Used with unlikely macro since this is only present in bugchecking or other high level scenarios.
    void* owner = InterlockedCompareExchangePointer((volatile void* volatile*)&ExclusiveOwnerShip, NULL, NULL);
    if (unlikely(owner && owner != MeGetCurrentProcessor())) return;

    bool prev_if = interrupts_enabled();
    acquire_tmp_lock(&gop_lock);
    __cli();
    GOP_PARAMS* gop = &gop_local;
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
            case 'x': gop_print_hex_minimal(gop, va_arg(ap, uint64_t), color); break;
            case 'p': gop_print_hex(gop, (uint64_t)(uintptr_t)va_arg(ap, void*), color); break;
            case 'c':
                gop_put_char(gop, (char)va_arg(ap, int), color);    // chars promoted to int
                break;
            case 'b':
                gop_print_binary(gop, va_arg(ap, uint64_t), color); // if 'b' means 64-bit binary
                break;
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
    release_tmp_lock(&gop_lock);
    if (prev_if) __sti();
}

void MgAcquireExclusiveGopOwnerShip(void) {
    // Set the pointer of exclusive ownership.
    InterlockedExchangePointer(&ExclusiveOwnerShip, (void*)MeGetCurrentProcessor());
}

void MgReleaseExclusiveGopOwnerShip(void) {
    // Trust the caller, just set the ExclusiveOwnerShip pointer to NULL.
    InterlockedExchangePointer(&ExclusiveOwnerShip, NULL);
}