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

/*++

    Routine description:

        Saves the nonvolatile language context and returns twice when a
        continuation is restored.

    Arguments:

        [OUT] Context - The context storage to populate.

    Return Values:

        Zero on the initial return, or the restored continuation value on the
        second return.

--*/
MTDLL_API RETURNS_TWICE
int
MtpSaveLanguageContext(
    PMT_LANGUAGE_CONTEXT Context
);

/*++

    Routine description:

        Links a language exception frame into the current TEB exception chain.

    Arguments:

        [IN OUT] Frame - The frame to link.

    Return Values:

        None.

--*/
MTDLL_API
void
MtpEnterLanguageFrame(
    PMT_LANGUAGE_FRAME Frame
);

/*++

    Routine description:

        Removes a language exception frame from the current TEB exception
        chain.

    Arguments:

        [IN OUT] Frame - The frame to unlink.

    Return Values:

        None.

--*/
MTDLL_API
void
MtpLeaveLanguageFrame(
    PMT_LANGUAGE_FRAME Frame
);

/*++

    Routine description:

        Applies a language filter result and transfers control to the selected
        continuation path.

    Arguments:

        [IN OUT] Frame - The active language frame.
        [IN] FilterResult - The filter disposition returned by user code.

    Return Values:

        Does not return.

--*/
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
