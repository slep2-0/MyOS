/*
 * PROJECT:     MatanelOS Kernel
 * LICENSE:     NONE
 * PURPOSE:     GOP Driver to draw onto screen (long-mode framebuffer)
 */

#ifndef X86_GOP_DRIVER_H
#define X86_GOP_DRIVER_H

 // Standard headers, required.
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../includes/stdarg_myos.h"
#include "../../trace.h"
#include "../../core/memory/memory.h"
#include "../../core/uefi_memory.h"

void gop_printf(uint32_t color, const char* fmt, ...);

#define gop_printf_forced(color, fmt, ...) gop_printf(color, fmt, ##__VA_ARGS__)
void gop_clear_screen(GOP_PARAMS* gop, uint32_t color);
#endif // X86_GOP_DRIVER_H
