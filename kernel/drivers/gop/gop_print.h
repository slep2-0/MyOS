// gop_print.h
#ifndef GOP_PRINT_H
#define GOP_PRINT_H

#include "gop.h"

void gop_text_init(GOP_PARAMS* gop, uint32_t color);
void gop_text(const char* s, uint32_t x, uint32_t y);

#endif
