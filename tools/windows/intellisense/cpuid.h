#ifndef MATANELOS_INTELLISENSE_CPUID_H
#define MATANELOS_INTELLISENSE_CPUID_H

/* GCC's cpuid.h is unavailable to the MSVC language service. */
#define __cpuid(level, eax, ebx, ecx, edx) \
    do {                                      \
        (void)(level);                        \
        (eax) = 0;                            \
        (ebx) = 0;                            \
        (ecx) = 0;                            \
        (edx) = 0;                            \
    } while (0)

#endif
