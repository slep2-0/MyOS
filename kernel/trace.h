// trace.h
#ifndef TRACE_H
#define TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "intrinsics/intrin.h"
#include "includes/me.h"

#ifdef DEBUG
static inline PPROCESSOR thisCPUtmp(void) {
    return (PPROCESSOR)__readgsqword(0);
}
#endif

// this stays `static inline` so every .c gets its own copy
static inline void tracelast_func(const char* function_name) {
#ifdef GDB
    UNREFERENCED_PARAMETER(function_name);
    return;
#elif defined(DEBUG)
    extern bool isBugChecking;

    if (!function_name || isBugChecking) return;

    PPROCESSOR cp = thisCPUtmp();
    if (!cp) return;

    LASTFUNC_HISTORY* lfh = thisCPUtmp()->lastfuncBuffer;
    if (!lfh) return;

    lfh->current_index = (lfh->current_index + 1) % LASTFUNC_HISTORY_SIZE;

    // Clear current slot
    for (size_t j = 0; j < LASTFUNC_BUFFER_SIZE; j++)
        lfh->names[lfh->current_index][j] = 0;

    // Copy function name
    for (size_t i = 0; i < LASTFUNC_BUFFER_SIZE - 1 && function_name[i]; i++)
        lfh->names[lfh->current_index][i] = (uint8_t)function_name[i];

#else
    UNREFERENCED_PARAMETER(function_name);
    return;
#endif
}

#endif // TRACE_H
