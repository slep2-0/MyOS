#ifndef X86_MATANEL_GRAPHICAL_H
#define X86_MATANEL_GRAPHICAL_H

/*++

Module Name:

    mg.h

Purpose:

    This module contains the header files & prototypes required for graphical interfaces. - Note that this only contains a very basic driver, advanced drivers should be loaded dynamically.

Author:

    slep (Matanel) 2025.

Revision History:

--*/


#include <stdint.h>
#include <stdbool.h>
#include "stdarg_myos.h"
#include "efi.h"

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

/*
void __attribute__((format(printf, 2, 3)))
gop_printf(uint32_t color, const char* fmt, ...);
*/

#ifdef DISABLE_GOP
static inline void __gop_mark_used(void* dummy, ...) {
    (void)dummy; /* silence unused-param warning for the first arg */
}

/* no-op gop_printf that still marks variables as used */
#define gop_printf(color, fmt, ...) \
    do { \
        (void)(color); \
        (void)(fmt); \
        __gop_mark_used(NULL, ##__VA_ARGS__); \
    } while (0)
#else
void __attribute__((format(printf, 2, 3)))
gop_printf(uint32_t color, const char* fmt, ...);
#endif


void gop_clear_screen(GOP_PARAMS* gop, uint32_t color);


int ksnprintf(char* buf, size_t bufsize, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
int kstrcmp(const char* s1, const char* s2);
int kstrncmp(const char* s1, const char* s2, size_t length);
size_t kstrlen(const char* str);
char* kstrcpy(char* dst, const char* src);
// Gurantees null termination.
char* kstrncpy(char* dst, const char* src, size_t n);
char* kstrtok_r(char* str, const char* delim, char** save_ptr);
char* kstrncat(char* dest, const char* src, size_t max_len);


void MgAcquireExclusiveGopOwnerShip(void);
void MgReleaseExclusiveGopOwnerShip(void);

#endif