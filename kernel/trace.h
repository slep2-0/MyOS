// trace.h
#ifndef TRACE_H
#define TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "intrin/intrin.h"

#define LASTFUNC_BUFFER_SIZE 128
#define LASTFUNC_HISTORY_SIZE 25

typedef struct {
    uint8_t names[LASTFUNC_HISTORY_SIZE][LASTFUNC_BUFFER_SIZE];
    int     current_index;
} LASTFUNC_HISTORY;

// this stays `static inline` so every .c gets its own copy
static void tracelast_func(const char* function_name) {
#ifdef GDB
    // if GDB is defined, we wont trace last functions, as it is VERY hard to debug with having to step 128 times.
    UNREFERENCED_PARAMETER(function_name);
    return;
#elif !defined(DEBUG)
    extern bool isBugChecking;
    extern LASTFUNC_HISTORY lastfunc_history;

    if (!function_name || isBugChecking) return;

    lastfunc_history.current_index =
        (lastfunc_history.current_index + 1) % LASTFUNC_HISTORY_SIZE;

    // Clear entire buffer slot to 0 to avoid leftover garbage
    for (size_t j = 0; j < LASTFUNC_BUFFER_SIZE; j++) {
        lastfunc_history.names[lastfunc_history.current_index][j] = 0;
    }

    // Copy function_name safely
    for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++) {
        lastfunc_history.names[lastfunc_history.current_index][i] =
            (uint8_t)function_name[i];
    }
    // Explicit null terminator already guaranteed by zero clear
#else
    UNREFERENCED_PARAMETER(function_name);
    return;
#endif
}

#endif // TRACE_H
