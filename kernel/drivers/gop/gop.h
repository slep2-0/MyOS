/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     GOP Driver to draw onto screen (long-mode framebuffer)
 */

#ifndef X86_GOP_DRIVER_H
#define X86_GOP_DRIVER_H

/// Colors definitions for easier access
#define COLOR_RED        0xFFFF0000
#define COLOR_GREEN      0xFF00FF00
#define COLOR_BLUE       0xFF0000FF
#define COLOR_WHITE      0xFFFFFFFF
#define COLOR_BLACK      0xFF000000
#define COLOR_YELLOW     0xFFFFFF00
#define COLOR_CYAN       0xFF00FFFF
#define COLOR_MAGENTA    0xFFFF00FF
#define COLOR_GRAY       0xFF808080
#define COLOR_DARK_GRAY  0xFF404040
#define COLOR_LIGHT_GRAY 0xFFD3D3D3
#define COLOR_ORANGE     0xFFFFA500
#define COLOR_BROWN      0xFFA52A2A
#define COLOR_PURPLE     0xFF800080
#define COLOR_PINK       0xFFFFC0CB
#define COLOR_LIME       0xFF32CD32
#define COLOR_NAVY       0xFF000080
#define COLOR_TEAL       0xFF008080
#define COLOR_OLIVE      0xFF808000

 // Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../includes/stdarg_myos.h"
#include "../../trace.h"
#include "../../core/memory/memory.h"
#include "../../core/uefi_memory.h"

 // integer font scale (1 = native 8×16, 2 = 16×32, etc)
#define FONT_SCALE 1

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

void draw_char(GOP_PARAMS* gop, char c, uint32_t x, uint32_t y, uint32_t color);
void draw_string(GOP_PARAMS* gop, const char* s, uint32_t x, uint32_t y, uint32_t color);

void gop_printf(uint32_t color, const char* fmt, ...);

#define gop_printf_forced(color, fmt, ...) gop_printf(color, fmt, ##__VA_ARGS__)

void gop_put_char(GOP_PARAMS* gop, char c, uint32_t color);
void gop_puts(GOP_PARAMS* gop, const char* s, uint32_t color);
void gop_scroll(GOP_PARAMS* gop);
void gop_clear_screen(GOP_PARAMS* gop, uint32_t color);
void gop_print_dec(GOP_PARAMS* gop, unsigned val, uint32_t color);
void gop_print_hex(GOP_PARAMS* gop, uint64_t val, uint32_t color);

/// <summary>
/// snprintf equivalent. -- Safe to run at ANY IRQL.
/// </summary>
/// <param name="buf">Buffer to format.</param>
/// <param name="bufSize">Size of the buffer.</param>
/// <param name="fmt">String and format.</param>
/// <return>Amount of bytes written. (excluding null terminator)</return>
int ksnprintf(char* buf, size_t bufsize, const char* fmt, ...);

// Compare two null-terminated strings.
// Returns 0 if equal, <0 if s1 < s2, >0 if s1 > s2.
int kstrcmp(const char* s1, const char* s2);

// Compare two null-terminated strings with a maximum length to adhere.
// Returns 0 if equal up to length, <0 if s1 < s2, >0 if s1 > s2.
int kstrncmp(const char* s1, const char* s2, size_t length);

size_t kstrlen(const char* str);
char* kstrcpy(char* dst, const char* src);
char* kstrncpy(char* dst, const char* src, size_t n);
char* kstrtok(char* str, const char* delim);

/// <summary>
/// Concatenates src onto dest, up to max_len total bytes in dest.
/// </summary>
/// <param name="dest">Destination buffer (must be mutable)</param>
/// <param name="src">String to append</param>
/// <param name="max_len">Total size of the destination buffer</param>
/// <returns>Pointer to dest</returns>
char* kstrncat(char* dest, const char* src, size_t max_len);

#endif // X86_GOP_DRIVER_H
