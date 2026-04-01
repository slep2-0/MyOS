#pragma once

#include "mtdll.h"


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

void printf(uint32_t Color, const char* fmt, ...);