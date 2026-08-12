#ifndef MATANELOS_MTDLL_PRIVATE_EXPORTS_H
#define MATANELOS_MTDLL_PRIVATE_EXPORTS_H

/*
 * Private MTDLL loader entrypoints.
 *
 * Stable application APIs are declared once in shared/include/MatanelOS.h.
 * Keep only runtime exports that require private PEB/TEB types in this file.
 */

#include "mtdll.h"

MTDLL_API void
LdrInitializeProcess(
    IN PPEB InitialPeb,
    IN PTEB InitialTeb,
    IN uint64_t EntryPoint,
    IN PMTDLL_BASIC_TYPES BasicTypes
);

MTDLL_API void
LdrInitializeThread(
    IN PTEB Teb,
    IN PPEB Peb,
    IN uint64_t EntryPoint,
    IN uintptr_t ThreadParameter
);

NORETURN void
MeUserExceptionDispatcher(
    PEXCEPTION_RECORD ExceptionRecord,
    PCONTEXT ContextRecord
);

#endif /* MATANELOS_MTDLL_PRIVATE_EXPORTS_H */
