/*
 * PROJECT:      MatanelOS
 * LICENSE:      GPLv3
 * PURPOSE:      Compiler runtime contract for frame-based user exceptions.
 */

#ifndef MATANELOS_SHARED_MTLANGUAGE_H
#define MATANELOS_SHARED_MTLANGUAGE_H

#include "annotations.h"
#include "mtapi.h"
#include "mtexception.h"

#ifdef __cplusplus
extern "C" {
#endif

MTDLL_API RETURNS_TWICE
int
MtpSaveLanguageContext(
    PMT_LANGUAGE_CONTEXT Context
);

MTDLL_API
void
MtpEnterLanguageFrame(
    PMT_LANGUAGE_FRAME Frame
);

MTDLL_API
void
MtpLeaveLanguageFrame(
    PMT_LANGUAGE_FRAME Frame
);

MTDLL_API NORETURN
void
MtpApplyLanguageFilter(
    PMT_LANGUAGE_FRAME Frame,
    int FilterResult
);

typedef struct _MT_LANGUAGE_SCOPE_GUARD {
    PMT_LANGUAGE_FRAME Frame;
} MT_LANGUAGE_SCOPE_GUARD, * PMT_LANGUAGE_SCOPE_GUARD;

FORCEINLINE
void
MtpCleanupLanguageFrame(
    PMT_LANGUAGE_SCOPE_GUARD Guard
)
{
    if (Guard->Frame && Guard->Frame->Linked == 1) {
        MtpLeaveLanguageFrame(Guard->Frame);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* MATANELOS_SHARED_MTLANGUAGE_H */
